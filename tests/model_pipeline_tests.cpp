#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include "model_render_resource.hpp"
#include "model_selection.hpp"
#include <forge/model_asset.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why, std::source_location at = std::source_location::current()) {
    if (!ok)
        throw std::runtime_error(std::string(why) + " at " + std::to_string(at.line()));
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
#include "model_animation_runtime.hpp"
#include "model_placement.hpp"
#include "model_placement_tests.hpp"
#include <forge/render_view.hpp>
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
                [decisions](auto& c, const auto& p, const auto& catalog) {
                    prepare_model_publication(c, p, catalog, decisions);
                },
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
        require(before.size() == 30, "Complete model family missing");
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
        auto loaded_model = load_model_selection(root, first.publication->catalog, owner);
        require(loaded_model.bindings == before &&
                    loaded_model.revision == first.publication->artifact.key,
                "Selected immutable model binding mismatch");
        for (const auto& [address, id] : before) {
            const auto& member = loaded_model.member(id);
            require(member.identity.address == address, "Selected model member address changed");
            if (member.node) {
                require(member.identity.type == ModelNodeAsset::type &&
                            *member.node < loaded_model.index.hierarchy.at("nodes").size(),
                        "Selected model node provenance unavailable");
                rejects([&] { loaded_model.bytes(member); });
            } else
                require(!loaded_model.bytes(member).empty(),
                        "Selected model member bytes unavailable");
        }
        test_model_placement(loaded_model, first.publication->catalog);
        ResourcePool<MeshAsset> mesh_resources({1, 64, 64, 128 * 1024 * 1024});
        ResourcePool<MaterialAsset> material_resources({1, 64, 64, 16 * 1024 * 1024});
        const AssetRef<MeshAsset> selected_mesh{before.at("/meshes/0")};
        auto first_catalog = std::make_shared<const AssetCatalog>(first.publication->catalog);
        auto mesh_request = request_model_mesh(mesh_resources, root, first_catalog, selected_mesh);
        auto coalesced = request_model_mesh(mesh_resources, root, first_catalog, selected_mesh);
        require(mesh_request.inspect().identity == coalesced.inspect().identity,
                "Identical model mesh requests did not coalesce");
        require(mesh_resources.wait(mesh_request, 10s), "Model mesh resource failed");
        auto mesh_lease = mesh_resources.acquire(mesh_request);
        require(mesh_lease && mesh_lease->materials.size() == 1 &&
                    mesh_lease->materials[0].material.id == before.at("/materials/0"),
                "Model mesh lost logical material binding");
        const auto first_slot = mesh_lease->materials[0];
        const auto cooked_material =
            decode_material(loaded_model.bytes(loaded_model.member(first_slot.material.id)));
        MaterialLayout material_layout;
        material_layout.model = cooked_material.model;
        for (const auto& [key, field] : cooked_material.parameters)
            material_layout.parameters[key] = field.type;
        for (const auto& [key, field] : cooked_material.textures)
            material_layout.textures[key] = {field.semantic, field.dimension, true};
        auto material_request = request_model_material(material_resources, root, first_catalog,
                                                       first_slot.material, material_layout);
        require(material_resources.wait(material_request, 10s), "Model material resource failed");
        auto material_lease = material_resources.acquire(material_request);
        require(material_lease && material_lease->values == cooked_material &&
                    material_lease->textures.size() == cooked_material.textures.size(),
                "Model material values or typed texture bindings differ");
        auto builtin_request = request_model_pbr_material(material_resources, root, first_catalog,
                                                          first_slot.material);
        auto builtin_coalesced = request_model_pbr_material(material_resources, root, first_catalog,
                                                            first_slot.material);
        require(
            builtin_request.inspect().identity == builtin_coalesced.inspect().identity &&
                builtin_request.inspect().identity.variant !=
                    material_request.inspect().identity.variant &&
                material_resources.wait(builtin_request, 10s),
            "Built-in material request did not validate/coalesce independently of explicit layout");
        auto builtin_lease = material_resources.acquire(builtin_request);
        require(builtin_lease->values == material_lease->values &&
                    builtin_lease->textures == material_lease->textures,
                "Built-in material preparation changed authored values or bindings");
        ResourcePool<TextureAsset> texture_resources({1, 64, 64, 64 * 1024 * 1024});
        require(!material_lease->textures.empty(), "Official material lost texture fixture");
        const auto& [texture_role, texture_ref] = *material_lease->textures.begin();
        const auto texture_semantic = material_lease->values.textures.at(texture_role).semantic;
        auto texture_request = request_model_texture(texture_resources, root, first_catalog,
                                                     texture_ref, texture_semantic);
        require(texture_resources.wait(texture_request, 10s), "Model texture resource failed");
        auto texture_lease = texture_resources.acquire(texture_request);
        require(texture_lease && texture_lease->semantic == texture_semantic &&
                    texture_lease.identity().revision == material_lease.identity().revision &&
                    texture_lease.identity().revision == mesh_lease.identity().revision,
                "Model render resource family mixed revisions or color semantics");
        const auto image_bytes = texture_lease->byte_size();
        auto missing_variant = request_model_texture(texture_resources, root, first_catalog,
                                                     texture_ref, TextureSemantic::HdrColor);
        require(!texture_resources.wait(missing_variant, 10s) &&
                    texture_lease->byte_size() == image_bytes,
                "Unavailable texture variant replaced valid color data");
        auto wrong_layout = material_layout;
        wrong_layout.model = "incompatible.model";
        auto rejected_layout = request_model_material(material_resources, root, first_catalog,
                                                      first_slot.material, wrong_layout);
        require(rejected_layout.inspect().identity.variant !=
                        material_request.inspect().identity.variant &&
                    !material_resources.wait(rejected_layout, 10s) && material_lease,
                "Incompatible layout coalesced or invalidated good material");
        std::vector<MaterialSlotOverride> material_overrides{
            {first_slot.key, {before.at("/materials/1")}}};
        const auto overridden = select_mesh_materials(*mesh_lease.operator->(), material_overrides);
        require(overridden.unresolved.empty() &&
                    overridden.bindings[0].material == material_overrides[0].material,
                "Material override not resolved through stable binding key");
        auto wrong_catalog = first.publication->catalog;
        auto wrong_record = wrong_catalog.records().at(before.begin()->second);
        wrong_record.metadata["forge.import"]["generation"] = 999u;
        wrong_catalog.replace(wrong_record);
        rejects([&] { load_model_selection(root, wrong_catalog, owner); });
        std::stop_source load_cancel;
        load_cancel.request_stop();
        rejects([&] {
            load_model_selection(root, first.publication->catalog, owner, load_cancel.get_token());
        });
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
                             std::to_string(group == "nodes"
                                                ? index
                                                : reordered.at(group).size() - 1 - index)) == id,
                    "Reorder/rename retargeted logical subasset");
        }
        auto new_catalog = std::make_shared<const AssetCatalog>(shuffled.publication->catalog);
        auto mesh_reimport = request_model_mesh(mesh_resources, root, new_catalog, selected_mesh);
        require(mesh_resources.wait(mesh_reimport, 10s), "Reordered model mesh resource failed");
        auto new_mesh_lease = mesh_resources.acquire(mesh_reimport);
        const auto remapped_materials =
            select_mesh_materials(*new_mesh_lease.operator->(), material_overrides);
        require(new_mesh_lease->materials[0].key == first_slot.key &&
                    new_mesh_lease->materials[0].physical_slot != first_slot.physical_slot &&
                    remapped_materials.unresolved.empty() &&
                    remapped_materials.bindings[0].material == material_overrides[0].material &&
                    mesh_lease->materials[0] == first_slot,
                "Source reorder retargeted override or mutated a pinned old mesh");
        auto invalid_catalog = std::make_shared<AssetCatalog>(shuffled.publication->catalog);
        auto bad_mesh_record = invalid_catalog->records().at(selected_mesh.id);
        bad_mesh_record.metadata["forge.model"]["sha256"] = std::string(64, '0');
        invalid_catalog->replace(bad_mesh_record);
        // A different pool forces verification instead of reusing an admitted revision.
        ResourcePool<MeshAsset> invalid_pool;
        auto bad_mesh_request =
            request_model_mesh(invalid_pool, root, invalid_catalog, selected_mesh);
        require(!invalid_pool.wait(bad_mesh_request, 10s) && new_mesh_lease && mesh_lease,
                "Bad model metadata accepted or existing mesh leases invalidated");
        auto bad_replacement = std::make_shared<AssetCatalog>(shuffled.publication->catalog);
        std::vector<AssetRecord> next_records;
        for (const auto& [id, record] : bad_replacement->records()) {
            (void)id;
            if (record.metadata.contains("forge.import")) {
                auto next = record;
                next.metadata["forge.import"]["generation"] =
                    next.metadata["forge.import"]["generation"].get<std::uint64_t>() + 1;
                next.metadata["forge.import"]["artifact_digest"] = std::string(64, '0');
                next_records.push_back(std::move(next));
            }
        }
        for (auto& record : next_records)
            bad_replacement->replace(record);
        auto bad_replacement_request =
            request_model_mesh(mesh_resources, root, bad_replacement, selected_mesh);
        require(!mesh_resources.wait(bad_replacement_request, 10s) &&
                    mesh_resources.current(selected_mesh).identity() == new_mesh_lease.identity(),
                "Invalid next model family replaced previous good mesh");
        // Reorder all source nodes without changing their graph, then rename them.
        // Address changes must not become persistent provenance changes.
        const auto old_node_count = reordered.at("nodes").size();
        std::reverse(reordered["nodes"].begin(), reordered["nodes"].end());
        for (auto& node : reordered["nodes"]) {
            node["name"] = "Renamed source node";
            if (node.contains("children"))
                for (auto& child : node["children"])
                    child = old_node_count - 1 - child.template get<std::size_t>();
        }
        for (auto& scene : reordered["scenes"])
            for (auto& node : scene["nodes"])
                node = old_node_count - 1 - node.template get<std::size_t>();
        save(root / source, reordered);
        auto nodes_reordered = run(source);
        if (!nodes_reordered.published) {
            // This sample has identical geometry allocated to separate meshes.
            // Renaming every usage also changes their prior semantic evidence.
            // Resolve those actual reported mesh conflicts explicitly; this is
            // fixture-known correspondence, never a production index heuristic.
            require(!nodes_reordered.identity_conflicts.empty(),
                    nodes_reordered.diagnostic.c_str());
            std::vector<SubassetIdentityDecision> mesh_choices;
            for (const auto& conflict : nodes_reordered.identity_conflicts) {
                require(conflict.type == "mesh", "Unexpected node identity conflict");
                for (const auto& address : conflict.observations)
                    mesh_choices.push_back({address, after.at(address)});
            }
            nodes_reordered = run(source, mesh_choices);
        }
        require(nodes_reordered.published, nodes_reordered.diagnostic.c_str());
        const auto node_bindings = bindings(nodes_reordered.publication->catalog, owner);
        for (std::size_t i = 0; i < old_node_count; ++i)
            require(node_bindings.at("/nodes/" + std::to_string(old_node_count - 1 - i)) ==
                        after.at("/nodes/" + std::to_string(i)),
                    "Source node reorder/rename retargeted durable provenance");
        auto wrong_node_catalog = nodes_reordered.publication->catalog;
        auto wrong_node = wrong_node_catalog.records().at(node_bindings.at("/nodes/0"));
        wrong_node.metadata["forge.model"]["node"] = 99999;
        wrong_node_catalog.replace(wrong_node);
        rejects([&] { load_model_selection(root, wrong_node_catalog, owner); });
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
        const auto first_node = bindings(one.publication->catalog, small_owner).at("/nodes/0");
        doc["nodes"][0]["name"] = "Renamed uniquely used node";
        doc["nodes"][0]["translation"] = {3, 2, 1};
        doc["nodes"][0]["scale"] = {-2, 0, .5};
        save(root / small, doc);
        auto moved_node = run(small);
        require(moved_node.published &&
                    bindings(moved_node.publication->catalog, small_owner).at("/nodes/0") ==
                        first_node,
                "Source transform edit lost uniquely evidenced node identity");
        auto mesh = doc["meshes"][0];
        doc["meshes"].push_back(mesh);
        doc["meshes"].push_back(mesh);
        save(root / small, doc);
        auto three = run(small);
        require(three.published, three.diagnostic.c_str());
        const auto ids = bindings(three.publication->catalog, small_owner);
        require(ids.size() == 4 && ids.at("/meshes/0") == first_id,
                "New members replaced known identity");
        auto unchanged = run(small);
        require(unchanged.published && unchanged.cache_hit &&
                    bindings(unchanged.publication->catalog, small_owner) == ids,
                "Exact unchanged import asked to remap identical members");
        auto baseline = read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        doc["asset"]["generator"] = "Changed source requires fresh correspondence evidence";
        save(root / small, doc);
        auto ambiguous = run(small);
        require(!ambiguous.published && !ambiguous.cache_hit &&
                    ambiguous.identity_conflicts.size() == 1 &&
                    ambiguous.identity_conflicts[0].observations.size() == 2 &&
                    read_bytes(root / "forge.assets.json", max_asset_index_bytes) == baseline,
                "Changed ambiguous source guessed an identity");
        std::vector<SubassetIdentityDecision> decisions{{"/meshes/1", ids.at("/meshes/1")},
                                                        {"/meshes/2", ids.at("/meshes/2")}};
        auto resolved = run(small, decisions);
        require(resolved.published && bindings(resolved.publication->catalog, small_owner) == ids,
                "Explicit correspondence failed");
        // Exact cache correspondence must not overrule an explicit identity choice.
        auto swapped =
            run(small, {{"/meshes/1", ids.at("/meshes/2")}, {"/meshes/2", ids.at("/meshes/1")}});
        require(swapped.published && swapped.cache_hit &&
                    bindings(swapped.publication->catalog, small_owner).at("/meshes/1") ==
                        ids.at("/meshes/2"),
                "Unchanged import ignored explicit correspondence");
        auto restored = run(small, decisions);
        require(restored.published && bindings(restored.publication->catalog, small_owner) == ids,
                "Explicit correspondence could not restore bindings");
        auto explicit_new = run(small, {{"/meshes/1", std::nullopt}});
        require(explicit_new.published && explicit_new.cache_hit,
                "Unchanged import ignored explicit new identity");
        const auto new_bindings = bindings(explicit_new.publication->catalog, small_owner);
        require(new_bindings.at("/meshes/1") != ids.at("/meshes/1") &&
                    new_bindings.at("/meshes/0") == ids.at("/meshes/0") &&
                    new_bindings.at("/meshes/2") == ids.at("/meshes/2") &&
                    explicit_new.publication->catalog.records()
                        .at(ids.at("/meshes/1"))
                        .subasset->removed,
                "Explicit new identity changed unrelated members or lost its tombstone");
        auto restore_old = run(small, decisions);
        require(restore_old.published &&
                    bindings(restore_old.publication->catalog, small_owner) == ids,
                "Explicit choice could not restore a same-type tombstone");
        // A corrupted sidecar cannot borrow the catalog's unchanged-index proof.
        const auto exact_draft = service.prepare(small);
        const auto exact_plan = importer->discover(exact_draft.request, {});
        const auto pristine = AssetImportSidecar::parse(*exact_draft.ticket.sidecar_bytes);
        for (unsigned mutation = 0; mutation < 3; ++mutation) {
            AssetPublicationCandidate candidate;
            candidate.ticket = exact_draft.ticket;
            candidate.input = exact_plan.input;
            candidate.sidecar = pristine;
            candidate.files = restore_old.publication->artifact.files;
            auto& entry = *std::find_if(candidate.sidecar.identity.entries.begin(),
                                        candidate.sidecar.identity.entries.end(),
                                        [&](auto& e) { return e.id == ids.at("/meshes/1"); });
            if (mutation == 0)
                entry.removed = true;
            else if (mutation == 1)
                entry.evidence.content_digest = std::string(64, '0');
            else
                entry.key += "-mismatch";
            rejects([&] {
                prepare_model_publication(candidate, exact_plan, restore_old.publication->catalog);
            });
        }
        doc["meshes"].erase(doc["meshes"].begin() + 1, doc["meshes"].end());
        save(root / small, doc);
        auto removed = run(small);
        require(removed.published, removed.diagnostic.c_str());
        require(
            bindings(removed.publication->catalog, small_owner).size() == 2 &&
                removed.publication->catalog.records().at(ids.at("/meshes/1")).subasset->removed &&
                removed.publication->catalog.records().at(ids.at("/meshes/2")).subasset->removed,
            "Removed model members did not become tombstones");
        // Indistinguishable nodes may keep an exact revision, but changing source
        // requires decisions. Never use their source indices as persistent IDs.
        auto duplicates = doc;
        duplicates["nodes"] = Json::array({{{"mesh", 0}}, {{"mesh", 0}}});
        duplicates["scenes"][0]["nodes"] = {0, 1};
        const auto duplicate_source = std::filesystem::path("Assets/duplicate-nodes.gltf");
        save(root / duplicate_source, duplicates);
        auto duplicate_first = run(duplicate_source);
        require(duplicate_first.published, duplicate_first.diagnostic.c_str());
        const auto duplicate_owner = service.prepare(duplicate_source).request.asset;
        const auto duplicate_ids = bindings(duplicate_first.publication->catalog, duplicate_owner);
        const auto duplicate_repeat = run(duplicate_source);
        require(duplicate_repeat.published && duplicate_repeat.cache_hit &&
                    bindings(duplicate_repeat.publication->catalog, duplicate_owner) ==
                        duplicate_ids,
                "Unchanged duplicate nodes lost their selected identities");
        duplicates["asset"]["generator"] = "Different input with indistinguishable nodes";
        save(root / duplicate_source, duplicates);
        const auto duplicate_baseline =
            read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        auto node_conflict = run(duplicate_source);
        require(!node_conflict.published && node_conflict.identity_conflicts.size() == 1 &&
                    node_conflict.identity_conflicts[0].type == "model_node" &&
                    node_conflict.identity_conflicts[0].observations.size() == 2 &&
                    read_bytes(root / "forge.assets.json", max_asset_index_bytes) ==
                        duplicate_baseline,
                "Changed duplicate nodes were guessed or changed selected state");
        const auto node_resolved =
            run(duplicate_source, {{"/nodes/0", duplicate_ids.at("/nodes/1")},
                                   {"/nodes/1", duplicate_ids.at("/nodes/0")}});
        require(node_resolved.published &&
                    bindings(node_resolved.publication->catalog, duplicate_owner).at("/nodes/0") ==
                        duplicate_ids.at("/nodes/1"),
                "Explicit node correspondence was ignored");
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
        doc["meshes"][0]["primitives"][0]["targets"] = Json::array({{{"POSITION", 0}}});
        doc["meshes"][0]["weights"] = {0};
        doc["animations"][0]["samplers"].push_back({{"input", 1}, {"output", 1}});
        doc["animations"][0]["channels"].push_back(
            {{"sampler", 1}, {"target", {{"node", 0}, {"path", "weights"}}}});
        const auto animated_source = std::filesystem::path("Assets/animated.gltf");
        save(root / animated_source, doc);
        const auto animated = run(animated_source);
        require(animated.published, animated.diagnostic.c_str());
        const auto animated_owner = service.prepare(animated_source).request.asset;
        const auto members = bindings(animated.publication->catalog, animated_owner);
        require(members.size() == 7 && members.contains("/rig/skeleton") &&
                    members.contains("/animations/0") && members.contains("/animations/1"),
                "Animated family not published completely");
        for (const auto* address : {"/animations/0", "/animations/1"}) {
            const auto& record = animated.publication->catalog.records().at(members.at(address));
            require(record.type == "animation_clip" && record.dependency_edges.size() == 1 &&
                        record.dependency_edges[0].target == members.at("/rig/skeleton") &&
                        record.dependency_edges[0].expected_type == "skeleton",
                    "Clip did not receive typed skeleton dependency");
        }
        const auto loaded_animation =
            load_model_selection(root, animated.publication->catalog, animated_owner);
        require(loaded_animation.member(members.at("/rig/skeleton")).identity.type == "skeleton" &&
                    loaded_animation.member(members.at("/animations/0")).identity.type ==
                        "animation_clip",
                "Selected animated family typed members missing");
        model_animation_runtime(root, animated.publication->catalog, members);
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
        auto removed_clip = run(animated_source);
        if (!removed_clip.published) {
            // Removing the only scale clip removes the evidence distinguishing
            // two otherwise identical rest joints. Choose the known source joint
            // explicitly rather than treating its old source index as identity.
            require(!removed_clip.identity_conflicts.empty(), removed_clip.diagnostic.c_str());
            std::vector<SubassetIdentityDecision> node_choices;
            for (const auto& conflict : removed_clip.identity_conflicts) {
                require(conflict.type == "model_node", "Unexpected clip-removal conflict");
                for (const auto& address : conflict.observations)
                    node_choices.push_back({address, remapped.at(address)});
            }
            removed_clip = run(animated_source, node_choices);
        }
        require(removed_clip.published && removed_clip.publication->catalog.records()
                                              .at(members.at("/animations/1"))
                                              .subasset->removed,
                "Removed clip did not preserve tombstone");
        // A real camera/light-only glTF travels through the selected direct or
        // isolated recipe, publication, cooked selection and ordinary scene edit.
        const auto view_source = std::filesystem::path("Assets/views.gltf");
        const auto view_doc = Json::parse(R"({
          "asset":{"version":"2.0"},"extensionsUsed":["KHR_lights_punctual"],
          "extensions":{"KHR_lights_punctual":{"lights":[
            {"type":"spot","intensity":50,"range":10,"spot":{}},
            {"type":"point","intensity":12}, {"type":"directional","intensity":3}]}},
          "cameras":[
            {"type":"perspective","perspective":{"yfov":1,"znear":0.1}},
            {"type":"orthographic","orthographic":{"xmag":-4,"ymag":2,"znear":0,"zfar":100}}],
          "nodes":[
            {"name":"Perspective","camera":0,"translation":[0,1,6]},
            {"name":"Orthographic","camera":1},
            {"name":"Spot","scale":[-2,0,3],"extensions":{"KHR_lights_punctual":{"light":0}}},
            {"name":"Point","scale":[0,0,0],"extensions":{"KHR_lights_punctual":{"light":1}}},
            {"name":"Directional","extensions":{"KHR_lights_punctual":{"light":2}}}],
          "scenes":[{"nodes":[0,1,2,3,4]}],"scene":0
        })");
        save(root / view_source, view_doc);
        const auto views = run(view_source);
        require(views.published, views.diagnostic.c_str());
        const auto view_owner = service.prepare(view_source).request.asset;
        const auto view_model = load_model_selection(root, views.publication->catalog, view_owner);
        EngineContext view_engine;
        Scene view_scene(view_engine.world());
        view_scene.reset(empty_scene());
        const auto empty_views = view_scene.document();
        const auto placement =
            prepare_model_placement(view_model, view_scene.asset_id(), view_scene.revision());
        instantiate_model(view_scene, views.publication->catalog, placement);
        view_engine.world().evaluate_world_transforms();
        unsigned cameras = 0, lights = 0;
        const auto placed_views = view_scene.document();
        for (const auto& row : placed_views.at("entities")) {
            const auto entity = view_scene.entity(row.at("id"));
            const auto& world = entity.get<WorldTransform>().affine;
            if (entity.has<Camera>()) {
                const auto& camera = entity.get<Camera>();
                const auto frame = camera_view(camera, world, 800, 600);
                require(camera.basis == std::uint32_t(ViewBasis::GltfNegativeZ) &&
                            frame.forward == Double3{0, 0, -1},
                        "Model camera lost source basis or rewrote node orientation");
                if (camera.projection == 0)
                    require(camera.infinite_far, "Imported infinite camera became finite");
                else
                    require(camera.flip_x && !camera.flip_y && camera.orthographic_width == 8 &&
                                camera.orthographic_height == 4,
                            "Imported negative orthographic magnification lost semantics");
                ++cameras;
            }
            if (entity.has<Light>()) {
                const auto& light = entity.get<Light>();
                const auto frame = light_view(light, world);
                if (light.kind == 2)
                    require(frame.range == 10 && frame.intensity == 50 &&
                                frame.direction == Double3{0, 0, -1},
                            "Imported spot light changed physical quantities or direction");
                ++lights;
            }
        }
        require(cameras == 2 && lights == 3 && view_scene.entity_count() == 6,
                "Model camera/light placement lost nodes");
        const auto authored_views = view_scene.document();
        view_scene.undo();
        require(view_scene.document() == empty_views,
                "Camera/light placement undo left nodes behind");
        view_scene.redo();
        require(view_scene.document() == authored_views,
                "Camera/light placement redo changed identity");
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
