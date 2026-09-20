#include "asset_bytes.hpp"
#include "gltf_model_cook.hpp"
#include "gltf_snapshot.hpp"
#include "gltf_transform.hpp"
#include <algorithm>
#include <forge/texture_bundle.hpp>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid model bundle accepted");
}
ArtifactFile& file(std::vector<ArtifactFile>& files, const std::string& name) {
    for (auto& f : files)
        if (f.name == name)
            return f;
    throw std::runtime_error("Fixture file missing");
}
void change(std::vector<ArtifactFile>& files, const std::function<void(Json&)>& fn) {
    auto& bytes = file(files, "model.json").bytes;
    auto index = Json::parse(bytes);
    fn(index);
    const auto text = index.dump();
    const auto data = std::as_bytes(std::span(text));
    bytes.assign(data.begin(), data.end());
}
std::vector<SubassetObservation> observations(const ModelBundleIndex& index) {
    std::vector<SubassetObservation> result;
    for (const auto& m : index.members)
        result.push_back(m.identity);
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need official model fixture root");
        auto source = capture_gltf_source(argv[1], "NegativeScaleTest.gltf");
        const auto original_doc = source.document;
        const auto files = cook_static_gltf_bundle(NativeGltfDocument(source));
        const auto index = validate_model_bundle(files);
        require(index.members.size() == 30 && index.hierarchy.at("nodes").size() == 14,
                "Official static model family lost members/hierarchy");
        const auto again = cook_static_gltf_bundle(
            NativeGltfDocument(decode_gltf_snapshot(encode_gltf_snapshot(source))));
        require(files.size() == again.size(), "Model repeatability file count changed");
        for (std::size_t i = 0; i < files.size(); ++i)
            require(files[i].name == again[i].name && files[i].bytes == again[i].bytes,
                    "Model repeatability changed bytes");
        require(source.document == original_doc, "Cooking mutated source");
        SubassetIdentityDocument previous{AssetId::generate(), source.source, source.source_digest,
                                          "forge.model.static.v1"};
        auto first = reconcile_subassets(previous, source.source_digest, previous.evidence_schema,
                                         observations(index));
        require(first.document.has_value(), "Initial model identities failed");
        auto reordered = source;
        auto& d = reordered.document;
        const auto mesh_count = d.at("meshes").size(), material_count = d.at("materials").size(),
                   image_count = d.at("images").size();
        std::reverse(d["meshes"].begin(), d["meshes"].end());
        std::reverse(d["materials"].begin(), d["materials"].end());
        std::reverse(d["images"].begin(), d["images"].end());
        std::reverse(reordered.images.begin(), reordered.images.end());
        for (auto& node : d["nodes"])
            if (node.contains("mesh"))
                node["mesh"] = mesh_count - 1 - node["mesh"].get<std::size_t>();
        for (auto& mesh : d["meshes"]) {
            mesh["name"] = "Same renamed display label";
            for (auto& part : mesh["primitives"])
                if (part.contains("material"))
                    part["material"] = material_count - 1 - part["material"].get<std::size_t>();
        }
        for (auto& material : d["materials"])
            material["name"] = "Same material label";
        for (auto& texture : d["textures"])
            if (texture.contains("source"))
                texture["source"] = image_count - 1 - texture["source"].get<std::size_t>();
        reordered.source_digest = asset_build_digest(d);
        const auto changed_index =
            validate_model_bundle(cook_static_gltf_bundle(NativeGltfDocument(reordered)));
        const auto reconciled =
            reconcile_subassets(*first.document, reordered.source_digest, previous.evidence_schema,
                                observations(changed_index));
        require(reconciled.document.has_value(), "Reordered/renamed model became ambiguous");
        for (const auto& m : index.members) {
            if (m.node) {
                require(first.assignments.at(m.identity.address) ==
                            reconciled.assignments.at(m.identity.address),
                        "Mesh reorder changed source-node identity");
                continue;
            }
            const auto split = m.identity.address.find_last_of('/');
            const auto group = m.identity.address.substr(0, split + 1);
            const auto at = std::stoul(m.identity.address.substr(split + 1));
            const auto count = m.identity.type == "mesh"       ? mesh_count
                               : m.identity.type == "material" ? material_count
                                                               : image_count;
            require(first.assignments.at(m.identity.address) ==
                        reconciled.assignments.at(group + std::to_string(count - 1 - at)),
                    "Source reorder retargeted durable identity");
        }
        auto mixed = source;
        bool edited = false;
        for (auto& material : mixed.document["materials"]) {
            auto& pbr = material["pbrMetallicRoughness"];
            if (pbr.contains("baseColorTexture")) {
                pbr["metallicRoughnessTexture"] = pbr["baseColorTexture"];
                material["normalTexture"] = pbr["baseColorTexture"];
                edited = true;
                break;
            }
        }
        require(edited, "Official fixture lost textured material");
        auto mixed_files = cook_static_gltf_bundle(NativeGltfDocument(mixed));
        const auto mixed_index = validate_model_bundle(mixed_files);
        bool three = false;
        for (const auto& m : mixed_index.members)
            if (m.identity.type == "texture") {
                const auto bundle =
                    decode_texture_bundle_index(file(mixed_files, m.artifact.file).bytes);
                three = three || bundle.variants.size() == 3;
            }
        require(three && mixed_index.members.size() == index.members.size(),
                "Shared image usages created duplicate identities/lost variants");
        for (const auto& original : files) {
            auto bad = files;
            file(bad, original.name).bytes.pop_back();
            rejects([&] { validate_model_bundle(bad); });
        }
        auto invalid = [&](auto edit) {
            auto bad = files;
            change(bad, edit);
            rejects([&] { validate_model_bundle(bad); });
        };
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["parent"] = 0; });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["mesh"] = "/images/0"; });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["local"] = {1, 2, 3}; });
        invalid([](auto& j) { j["hierarchy"]["default_scene"] = 999; });
        invalid([](auto& j) { j["members"][0]["file"] = "../escape"; });
        invalid([](auto& j) { j["members"][0]["bindings"]["material.999"] = "/materials/0"; });
        invalid([](auto& j) { j["members"][0]["bindings"] = Json::object(); });
        invalid([](auto& j) { j["members"][0]["address"] = j["members"][1]["address"]; });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0].erase("trs"); });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["trs"]["translation"][0] = 999.0; });
        invalid([](auto& j) { j["hierarchy"]["nodes"][0]["trs"]["rotation"] = {0, 0, 0, 0}; });
        invalid([](auto& j) { j["members"].back()["node"] = 99999; });
        invalid([](auto& j) {
            j["members"].back()["node"] = j["members"][j["members"].size() - 2]["node"];
        });
        invalid([](auto& j) { j["members"].back()["file"] = "node.bin"; });
        invalid([](auto& j) { j["members"].erase(j["members"].end() - 1); });
        auto version2 = files;
        change(version2, [](auto& j) {
            j["version"] = 2;
            auto& members = j["members"];
            members.erase(
                std::remove_if(members.begin(), members.end(),
                               [](const auto& m) { return m.at("type") == "model_node"; }),
                members.end());
        });
        const auto legacy2 = validate_model_bundle(version2);
        require(legacy2.version == 2 && legacy2.hierarchy.at("nodes")[0].contains("trs") &&
                    decode_model_bundle_index(encode_model_bundle_index(legacy2)).version == 2,
                "Legacy format2 lost TRS or silently gained node identities");
        auto legacy = files;
        change(legacy, [](auto& j) {
            j["version"] = 1;
            auto& members = j["members"];
            members.erase(
                std::remove_if(members.begin(), members.end(),
                               [](const auto& m) { return m.at("type") == "model_node"; }),
                members.end());
            for (auto& node : j["hierarchy"]["nodes"])
                node.erase("trs");
        });
        const auto legacy_index = validate_model_bundle(legacy);
        require(legacy_index.version == 1 &&
                    decode_model_bundle_index(encode_model_bundle_index(legacy_index)).version == 1,
                "Legacy cooked model version was silently upgraded");
        auto zero_source = source;
        const Json zero_node = {{"mesh", 0},
                                {"rotation", {.2, .4, .4, .8}},
                                {"translation", {1, 2, 3}},
                                {"scale", {0, -2, 1e-100}}};
        zero_source.document["nodes"] = Json::array({zero_node});
        zero_source.document["scenes"] = Json::array({{{"nodes", {0}}}});
        zero_source.document["scene"] = 0;
        zero_source.source_digest = asset_build_digest(zero_source.document);
        const auto zero_files = cook_static_gltf_bundle(NativeGltfDocument(zero_source));
        const auto zero_index = validate_model_bundle(zero_files);
        require(zero_index.version == 3 &&
                    zero_index.hierarchy.at("nodes")[0].at("trs") == canonical_gltf_trs(zero_node),
                "Cooked model lost original singular signed/tiny TRS");
        auto damaged_zero = zero_files;
        change(damaged_zero, [](auto& j) { j["hierarchy"]["nodes"][0]["local"][2] = 0; });
        rejects([&] { validate_model_bundle(damaged_zero); });
        auto bad = files;
        bad.push_back({"unexpected.bin", {std::byte{0}}});
        rejects([&] { validate_model_bundle(bad); });
        auto bad_source = source;
        for (auto& m : bad_source.document["materials"]) {
            if (m.contains("pbrMetallicRoughness") &&
                m["pbrMetallicRoughness"].contains("baseColorTexture")) {
                m["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 999;
                break;
            }
        }
        rejects([&] { cook_static_gltf_bundle(NativeGltfDocument(bad_source)); });
        auto mislabeled = source;
        mislabeled.images[0].mime_type = "image/jpeg";
        rejects([&] { cook_static_gltf_bundle(NativeGltfDocument(mislabeled)); });
        mislabeled = source;
        mislabeled.document["extensionsUsed"] = {"EXT_texture_webp"};
        mislabeled.document["textures"][0]["extensions"]["EXT_texture_webp"] = {{"source", 0}};
        mislabeled.images[0].mime_type = "image/webp";
        rejects([&] { cook_static_gltf_bundle(NativeGltfDocument(mislabeled)); });
        GltfModelCookOptions invalid_options;
        invalid_options.maximum_texture_size = 0;
        rejects([&] { cook_static_gltf_bundle(NativeGltfDocument(source), invalid_options); });
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { cook_static_gltf_bundle(NativeGltfDocument(source), {}, stop.get_token()); });
        std::cout << "Native static model family, image semantics, identity reorder, hierarchy and "
                     "corrupt-output rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
