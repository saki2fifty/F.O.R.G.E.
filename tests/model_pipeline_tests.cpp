#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include <forge/model_asset.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn, std::source_location at = std::source_location::current()) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid model pipeline accepted at " + std::to_string(at.line()));
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(bool(out.write(reinterpret_cast<const char*>(bytes.data()),
                           std::streamsize(bytes.size()))) &&
                bool(out.flush()),
            "Fixture write failed");
}
void save(const std::filesystem::path& path, const Json& doc) {
    const auto text = doc.dump();
    write(path, std::as_bytes(std::span(text)));
}
class DirectImporter final : public AssetImporter {
    std::shared_ptr<const AssetImporter> delegate_;
    std::filesystem::path converter_;

  public:
    DirectImporter(std::shared_ptr<const AssetImporter> d, std::filesystem::path converter)
        : AssetImporter(d->descriptor(), d->settings()), delegate_(std::move(d)),
          converter_(std::move(converter)) {}
    ImportProbeResult probe(const ImportProbe& p) const override { return delegate_->probe(p); }
    AssetImportPlan discover(const AssetImportRequest& r, std::stop_token s) const override {
        return delegate_->discover(r, s);
    }
    void validate(const CachedArtifact& a) const override { delegate_->validate(a); }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& r, const AssetImportPlan& p, std::stop_token s,
                    const std::function<void(double, std::string)>&) const override {
        const auto current = discover(r, s);
        require(current.input.document() == p.input.document(), "Direct fixture input stale");
        auto files = execute_model_recipe(
            {{{"recipe", descriptor().id},
              {"revision", model_recipe_revision()},
              {"settings", r.settings},
              {"source_digest", p.input.source_digest},
              {"backend", r.target.backend}},
             encode_gltf_snapshot(
                 capture_gltf_source(r.project, r.source, model_cook_extensions(), {}, s), {}, s)},
            s);
        return finish_model_recipe(std::move(files), converter_, r.project, p.input.source_digest,
                                   p.data.at("converter_sha256").get<std::string>(), s);
    }
};
std::map<std::string, AssetId> bindings(const AssetCatalog& catalog, AssetId root) {
    std::map<std::string, AssetId> result;
    for (const auto& edge : catalog.records().at(root).dependency_edges)
        if (edge.role.starts_with("model.member:"))
            result.emplace(edge.role.substr(13), edge.target);
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 6, "Need mode worker fixture output-root converter");
        const bool direct = std::string_view(argv[1]) == "--direct";
        require(direct || std::string_view(argv[1]) == "--worker", "Invalid model test mode");
        const auto root = std::filesystem::absolute(argv[4]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        std::filesystem::copy(argv[3], root / "Assets/Model",
                              std::filesystem::copy_options::recursive);
        const auto converter = std::filesystem::absolute(argv[5]);
        auto importer = model_importer(std::filesystem::absolute(argv[2]), converter);
        if (direct)
            importer = std::make_shared<DirectImporter>(importer, converter);
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(importer);
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(root);
        AssetImportService service(lease, registry, {"portable", "none", "cpu"}, 1);
        auto run = [&](const std::filesystem::path& source,
                       std::vector<SubassetIdentityDecision> decisions = {},
                       bool reject_compatibility = false) {
            auto draft = service.prepare(source);
            service.submit(
                draft,
                [decisions](auto& c, const auto& p) { prepare_model_publication(c, p, decisions); },
                [reject_compatibility](const auto& catalog, const auto& artifact) {
                    require(!reject_compatibility, "Deliberate live compatibility failure");
                    require(!catalog.records().empty() && !artifact.files.empty(),
                            "Empty preflight");
                });
            require(service.wait_idle(60s), "Model service timed out");
            auto result = service.poll();
            require(result.size() == 1, "Missing import receipt");
            return std::move(result.front());
        };
        const auto source = std::filesystem::path("Assets/Model/NegativeScaleTest.gltf");
        auto first = run(source);
        require(first.published && !first.cache_hit, first.diagnostic.c_str());
        const auto owner = service.prepare(source).request.asset;
        require(first.publication->catalog.resolve(AssetRef<ModelAsset>{owner}).state ==
                    AssetState::Available,
                "Model root unavailable");
        auto before = bindings(first.publication->catalog, owner);
        require(before.size() == 16, "Complete model family missing");
        for (const auto& [address, id] : before) {
            (void)address;
            const auto& record = first.publication->catalog.records().at(id);
            require(record.subasset && record.subasset->owner == owner &&
                        !record.subasset->removed && record.metadata.contains("forge.model"),
                    "Invalid published model member");
            for (const auto& edge : record.dependency_edges)
                require(edge.revision == first.publication->artifact.key &&
                            first.publication->catalog.records().at(edge.target).type ==
                                edge.expected_type,
                        "Model member binding type/revision mismatch");
        }
        auto repeated = run(source);
        require(repeated.published && repeated.cache_hit &&
                    bindings(repeated.publication->catalog, owner) == before,
                "Model cache reimport changed identities");
        const auto selected = read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        const auto sidecar = AssetPublisher::sidecar_path(source);
        const auto selected_sidecar = read_bytes(root / sidecar, max_asset_index_bytes);
        auto incompatible = run(source, {}, true);
        require(!incompatible.published &&
                    read_bytes(root / "forge.assets.json", max_asset_index_bytes) == selected &&
                    read_bytes(root / sidecar, max_asset_index_bytes) == selected_sidecar,
                "Compatibility failure changed selected model family");
        auto original = Json::parse(read_bytes(root / source, 16 * 1024 * 1024));
        auto reordered = original;
        for (const auto* key : {"meshes", "materials", "images"})
            std::reverse(reordered[key].begin(), reordered[key].end());
        for (auto& node : reordered["nodes"])
            if (node.contains("mesh"))
                node["mesh"] = reordered["meshes"].size() - 1 - node["mesh"].get<std::size_t>();
        for (auto& mesh : reordered["meshes"]) {
            mesh["name"] = "Renamed duplicate display label";
            for (auto& part : mesh["primitives"])
                if (part.contains("material"))
                    part["material"] =
                        reordered["materials"].size() - 1 - part["material"].get<std::size_t>();
        }
        for (auto& material : reordered["materials"])
            material["name"] = "Renamed material";
        for (auto& texture : reordered["textures"])
            if (texture.contains("source"))
                texture["source"] =
                    reordered["images"].size() - 1 - texture["source"].get<std::size_t>();
        save(root / source, reordered);
        auto shuffled = run(source);
        require(shuffled.published, shuffled.diagnostic.c_str());
        const auto after = bindings(shuffled.publication->catalog, owner);
        for (const auto& [address, id] : before) {
            const auto split = address.find_last_of('/');
            const auto group = address.substr(1, split - 1);
            const auto index = std::stoul(address.substr(split + 1));
            require(after.at("/" + group + "/" +
                             std::to_string(reordered.at(group).size() - 1 - index)) == id,
                    "Reorder/rename retargeted logical subasset");
        }
        // Stale external image edits cannot use an old prepared plan.
        auto draft = service.prepare(source);
        const auto plan = importer->discover(draft.request, {});
        require(!plan.sources.empty(), "External model dependencies were not captured");
        const auto dependency = root / plan.sources.front().source;
        const auto original_bytes = read_bytes(dependency, 512 * 1024 * 1024);
        auto changed = original_bytes;
        changed.back() ^= std::byte{1};
        write(dependency, changed);
        rejects([&] { importer->import_and_cook(draft.request, plan, {}, {}); });
        write(dependency, original_bytes);
        std::stop_source cancelled;
        cancelled.request_stop();
        rejects([&] { importer->import_and_cook(draft.request, plan, cancelled.get_token(), {}); });
        const auto known_good = read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        auto broken = reordered;
        broken["meshes"][0]["primitives"][0]["attributes"]["POSITION"] = 999999;
        save(root / source, broken);
        auto failed = run(source);
        require(!failed.published &&
                    read_bytes(root / "forge.assets.json", max_asset_index_bytes) == known_good,
                "Malformed model replaced last good family");
        save(root / source, reordered);
        // Minimal authored triangle with initially one uniquely used mesh.
        const auto small = std::filesystem::path("Assets/small.gltf");
        std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
        write(root / "Assets/triangle.bin", std::as_bytes(std::span(positions)));
        Json doc = {
            {"asset", {{"version", "2.0"}}},
            {"buffers", Json::array({{{"byteLength", 36}, {"uri", "triangle.bin"}}})},
            {"bufferViews", Json::array({{{"buffer", 0}, {"byteLength", 36}}})},
            {"accessors", Json::array({{{"bufferView", 0},
                                        {"componentType", 5126},
                                        {"type", "VEC3"},
                                        {"count", 3},
                                        {"min", {0, 0, 0}},
                                        {"max", {1, 1, 0}}}})},
            {"meshes",
             Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}}}})}}})},
            {"nodes", Json::array({{{"mesh", 0}, {"name", "Used mesh"}}})},
            {"scenes", Json::array({{{"nodes", {0}}}})},
            {"scene", 0}};
        save(root / small, doc);
        auto one = run(small);
        require(one.published, one.diagnostic.c_str());
        const auto small_owner = service.prepare(small).request.asset;
        const auto first_id = bindings(one.publication->catalog, small_owner).at("/meshes/0");
        auto mesh = doc["meshes"][0];
        doc["meshes"].push_back(mesh);
        doc["meshes"].push_back(mesh);
        save(root / small, doc);
        auto three = run(small);
        require(three.published, three.diagnostic.c_str());
        const auto ids = bindings(three.publication->catalog, small_owner);
        require(ids.size() == 3 && ids.at("/meshes/0") == first_id,
                "New members replaced known identity");
        auto baseline = read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        auto ambiguous = run(small);
        require(!ambiguous.published && ambiguous.cache_hit &&
                    ambiguous.identity_conflicts.size() == 1 &&
                    ambiguous.identity_conflicts[0].observations.size() == 2 &&
                    read_bytes(root / "forge.assets.json", max_asset_index_bytes) == baseline,
                "Ambiguous cache reimport guessed an identity");
        std::vector<SubassetIdentityDecision> decisions{{"/meshes/1", ids.at("/meshes/1")},
                                                        {"/meshes/2", ids.at("/meshes/2")}};
        auto resolved = run(small, decisions);
        require(resolved.published && bindings(resolved.publication->catalog, small_owner) == ids,
                "Explicit correspondence failed");
        doc["meshes"].erase(doc["meshes"].begin() + 1, doc["meshes"].end());
        save(root / small, doc);
        auto removed = run(small);
        require(removed.published, removed.diagnostic.c_str());
        require(
            bindings(removed.publication->catalog, small_owner).size() == 1 &&
                removed.publication->catalog.records().at(ids.at("/meshes/1")).subasset->removed &&
                removed.publication->catalog.records().at(ids.at("/meshes/2")).subasset->removed,
            "Removed model members did not become tombstones");
        // Complete families larger than the old 256-file cache default must work.
        doc["meshes"] = Json::array();
        doc["nodes"] = Json::array();
        doc["scenes"][0]["nodes"] = Json::array();
        for (unsigned i = 0; i < 260; ++i) {
            doc["meshes"].push_back(mesh);
            doc["nodes"].push_back({{"mesh", i}, {"name", "Unique use " + std::to_string(i)}});
            doc["scenes"][0]["nodes"].push_back(i);
        }
        const auto many = std::filesystem::path("Assets/many.gltf");
        save(root / many, doc);
        auto large = run(many);
        require(large.published && large.publication->artifact.files.size() > 256,
                large.diagnostic.c_str());
        auto large_hit = run(many);
        require(large_hit.published && large_hit.cache_hit,
                "Large model family cache reimport failed");
        // One complete animated family through the same service/publication path.
        // Skin-bound geometry exercises ordered skin.joints -> Ozz palette binding.
        std::vector<float> animation_data{0, 1, 0, 0, 0, 1, 2, 3};
        const auto animation_bytes = std::as_bytes(std::span(animation_data));
        std::vector<std::byte> binary(animation_bytes.begin(), animation_bytes.end());
        const auto joint_offset = binary.size();
        for (unsigned vertex = 0; vertex < 3; ++vertex)
            for (unsigned joint : {0u, 1u, 0u, 0u})
                binary.push_back(std::byte(joint));
        const auto weight_offset = binary.size();
        const std::array<float, 12> weights{.5f, .5f, 0, 0, .5f, .5f, 0, 0, .5f, .5f, 0, 0};
        const auto wb = std::as_bytes(std::span(weights));
        binary.insert(binary.end(), wb.begin(), wb.end());
        write(root / "Assets/animation.bin", binary);
        doc["meshes"] = Json::array({mesh});
        doc["buffers"].push_back({{"uri", "animation.bin"}, {"byteLength", binary.size()}});
        doc["bufferViews"].push_back({{"buffer", 1}, {"byteOffset", 0}, {"byteLength", 8}});
        doc["bufferViews"].push_back({{"buffer", 1}, {"byteOffset", 8}, {"byteLength", 24}});
        doc["bufferViews"].push_back(
            {{"buffer", 1}, {"byteOffset", joint_offset}, {"byteLength", 12}});
        doc["bufferViews"].push_back(
            {{"buffer", 1}, {"byteOffset", weight_offset}, {"byteLength", 48}});
        doc["accessors"].push_back({{"bufferView", 1},
                                    {"componentType", 5126},
                                    {"type", "SCALAR"},
                                    {"count", 2},
                                    {"min", {0}},
                                    {"max", {1}}});
        doc["accessors"].push_back(
            {{"bufferView", 2}, {"componentType", 5126}, {"type", "VEC3"}, {"count", 2}});
        doc["accessors"].push_back(
            {{"bufferView", 3}, {"componentType", 5121}, {"type", "VEC4"}, {"count", 3}});
        doc["accessors"].push_back(
            {{"bufferView", 4}, {"componentType", 5126}, {"type", "VEC4"}, {"count", 3}});
        doc["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = 3;
        doc["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"] = 4;
        doc["nodes"] =
            Json::array({{{"name", "Mesh"}, {"mesh", 0}, {"skin", 0}, {"children", {1, 2}}},
                         {{"name", "Joint A"}},
                         {{"name", "Joint B"}}});
        doc["skins"] = Json::array({{{"joints", {1, 2}}}});
        doc["scenes"][0]["nodes"] = {0};
        auto clip = [&](unsigned node, std::string path, std::string label) {
            return Json{
                {"name", label},
                {"samplers", Json::array({{{"input", 1}, {"output", 2}}})},
                {"channels",
                 Json::array({{{"sampler", 0}, {"target", {{"node", node}, {"path", path}}}}})}};
        };
        doc["animations"] =
            Json::array({clip(1, "translation", "Move"), clip(2, "scale", "Scale")});
        const auto animated_source = std::filesystem::path("Assets/animated.gltf");
        save(root / animated_source, doc);
        const auto animated = run(animated_source);
        require(animated.published, animated.diagnostic.c_str());
        const auto animated_owner = service.prepare(animated_source).request.asset;
        const auto members = bindings(animated.publication->catalog, animated_owner);
        require(members.size() == 4 && members.contains("/rig/skeleton") &&
                    members.contains("/animations/0") && members.contains("/animations/1"),
                "Animated family not published completely");
        for (const auto* address : {"/animations/0", "/animations/1"}) {
            const auto& record = animated.publication->catalog.records().at(members.at(address));
            require(record.type == "animation_clip" && record.dependency_edges.size() == 1 &&
                        record.dependency_edges[0].target == members.at("/rig/skeleton") &&
                        record.dependency_edges[0].expected_type == "skeleton",
                    "Clip did not receive typed skeleton dependency");
        }
        const auto cached_animation = run(animated_source);
        require(cached_animation.published && cached_animation.cache_hit &&
                    bindings(cached_animation.publication->catalog, animated_owner) == members,
                "Animated cache hit changed identities");
        std::reverse(doc["animations"].begin(), doc["animations"].end());
        for (auto& animation : doc["animations"])
            animation["name"] = "Same renamed label";
        save(root / animated_source, doc);
        const auto renamed = run(animated_source);
        require(renamed.published, renamed.diagnostic.c_str());
        const auto remapped = bindings(renamed.publication->catalog, animated_owner);
        require(remapped.at("/animations/0") == members.at("/animations/1") &&
                    remapped.at("/animations/1") == members.at("/animations/0") &&
                    remapped.at("/rig/skeleton") == members.at("/rig/skeleton"),
                "Animation reorder/rename changed durable identity");
        const auto selected_animation =
            read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        auto wrong = doc;
        wrong["skins"][0]["joints"] = {1}; // prepared geometry still references skin joint 1
        save(root / animated_source, wrong);
        const auto bad_skin = run(animated_source);
        require(!bad_skin.published && read_bytes(root / "forge.assets.json",
                                                  max_asset_index_bytes) == selected_animation,
                "Incompatible skin replaced previous complete family");
        doc["animations"].erase(doc["animations"].begin());
        save(root / animated_source, doc);
        const auto removed_clip = run(animated_source);
        require(removed_clip.published && removed_clip.publication->catalog.records()
                                              .at(members.at("/animations/1"))
                                              .subasset->removed,
                "Removed clip did not preserve tombstone");
        if (std::filesystem::exists(root / ".forge/jobs"))
            require(std::filesystem::is_empty(root / ".forge/jobs"),
                    "Finished model staging remains");
        std::cout
            << "Model service, isolated/direct recipe, atomic family, cache, reorder, ambiguity, "
               "explicit resolution, removal, stale/cancel/failure retention passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
