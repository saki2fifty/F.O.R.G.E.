#pragma once
#include "gltf_lod.hpp"
#include "gltf_lod_fixture.hpp"
inline void check_lower_lod_uv_validation(const std::vector<ArtifactFile>& original,
                                          const ModelBundleIndex& original_index) {
    for (const auto& member : original_index.members) {
        if (member.identity.type != "mesh")
            continue;
        auto files = original;
        auto index = original_index;
        auto mesh = decode_mesh(file(files, member.artifact.file).bytes);
        if (std::none_of(mesh.lods[0].parts.begin(), mesh.lods[0].parts.end(),
                         [](const auto& part) { return part.find("TEXCOORD_0"); }))
            continue;
        auto& record = *std::find_if(index.members.begin(), index.members.end(), [&](auto& m) {
            return m.identity.address == member.identity.address;
        });
        mesh.lods.push_back(mesh.lods[0]);
        mesh.lods[1].screen_coverage = .5f;
        auto replace = [&] {
            auto& bytes = file(files, record.artifact.file).bytes;
            bytes = encode_mesh(mesh);
            record.artifact.digest = content_digest(bytes);
            record.artifact.bytes = bytes.size();
            file(files, "model.json").bytes = encode_model_bundle_index(index);
        };
        replace();
        (void)validate_model_bundle(files);
        for (auto& part : mesh.lods[1].parts)
            std::erase_if(part.streams,
                          [](const auto& s) { return s.semantic.starts_with("TEXCOORD_"); });
        replace();
        try {
            (void)validate_model_bundle(files);
        } catch (const std::exception& e) {
            require(std::string_view(e.what()).find("unavailable texture coordinates") !=
                        std::string_view::npos,
                    "Lower LOD rejected for an unrelated reason");
            return;
        }
        throw std::runtime_error("Lower LOD missing required material UVs was admitted");
    }
    throw std::runtime_error("Official fixture no longer exercises textured LOD validation");
}
// Included in the real model bundle suite: source admission, native cooking,
// immutable bundle validation and subasset reconciliation share production paths.
inline void check_model_lods() {
    auto source = gltf_lod_fixture();
    auto cook = [](auto value) {
        return cook_static_gltf_bundle(NativeGltfDocument(std::move(value)));
    };
    auto files = cook(source);
    const auto index = validate_model_bundle(files);
    const auto& lod = *std::find_if(index.members.begin(), index.members.end(),
                                    [](const auto& m) { return m.identity.address == "/lods/0"; });
    const auto mesh = decode_mesh(file(files, lod.artifact.file).bytes);
    require(mesh.lods.size() == 2 && mesh.lods[0].screen_coverage == 1 &&
                mesh.lods[1].screen_coverage == .4f && mesh.lods[0].parts.size() == 2 &&
                mesh.lods[1].parts.size() == 1,
            "Authored LOD geometry/thresholds were discarded");
    require(lod.bindings.at("material.1") == "/materials/0" &&
                lod.bindings.at("material.2") == "/materials/1" &&
                mesh.lods[1].parts[0].material_slot == 2,
            "LOD materials were aliased or dropped");
    require(index.hierarchy.at("nodes")[0].at("mesh") == "/lods/0" &&
                index.hierarchy.at("nodes")[2].at("mesh") == "/meshes/0" &&
                decode_mesh(file(files, "mesh-0.fmesh").bytes).lods.size() == 1,
            "LOD policy changed an unrelated use of its source mesh");
    require(!index.diagnostics.empty(), "Optional final cull hint was silently discarded");
    require(encode_mesh(decode_mesh(encode_mesh(mesh))) == encode_mesh(mesh),
            "Multi-LOD artifact failed round trip");
    auto implicit = source;
    implicit.document.erase("scenes");
    implicit.document.erase("scene");
    const auto implicit_index = validate_model_bundle(cook(implicit));
    require(implicit_index.hierarchy.at("scenes") == Json::array({{0, 2}}),
            "Implicit scene placed lower LOD alternatives as extra objects");
    auto morph = source;
    for (auto& m : morph.document["meshes"]) {
        m["weights"] = {.25};
        for (auto& p : m["primitives"])
            p["targets"] = Json::array({{{"POSITION", 0}}});
    }
    (void)validate_model_bundle(cook(morph));
    morph.document["meshes"][1]["weights"] = {.5};
    rejects([&] { (void)cook(morph); });
    auto defaults = source;
    defaults.document["nodes"][0].erase("extras");
    const auto groups = gltf_mesh_lods(NativeGltfDocument(defaults), gltf_scene_metadata(defaults));
    require(groups.size() == 1 && groups[0].coverage == std::vector<float>{1, .5f},
            "Missing optional LOD hints did not use documented policy");
    auto invalid = [&](const auto& change_source) {
        auto bad = source;
        change_source(bad.document);
        rejects([&] { (void)cook(std::move(bad)); });
    };
    invalid([](auto& j) { j["nodes"][0]["extensions"]["MSFT_lod"]["ids"] = {0}; });
    invalid([](auto& j) { j["nodes"][0]["extensions"]["MSFT_lod"]["ids"] = {1, 1}; });
    invalid([](auto& j) { j["nodes"][0]["extensions"]["MSFT_lod"]["ids"] = {999}; });
    invalid([](auto& j) { j["nodes"][0]["extras"]["MSFT_screencoverage"] = {.3, .5}; });
    invalid([](auto& j) { j["nodes"][0]["extras"]["MSFT_screencoverage"] = {.3}; });
    invalid([](auto& j) { j["nodes"][1]["translation"] = {1, 0, 0}; });
    invalid([](auto& j) { j["nodes"][1]["children"] = {2}; });
    invalid([](auto& j) { j["nodes"][2]["children"] = {1}; });
    invalid([](auto& j) { j["nodes"][1]["extensions"]["MSFT_lod"]["ids"] = {0}; });
    invalid([](auto& j) { j["materials"][0]["extensions"]["MSFT_lod"]["ids"] = {1}; });
    // Every level still obeys the same physical material-slot contract.
    auto broken = mesh;
    broken.lods[1].parts[0].material_slot = 999;
    rejects([&] { (void)encode_mesh(broken); });
    auto skin_source = source;
    auto skin_bytes = std::make_shared<std::vector<std::byte>>(source.buffers[0].bytes().begin(),
                                                               source.buffers[0].bytes().end());
    auto skin_attribute = [&](unsigned type, unsigned bytes_per_component) {
        const auto view = skin_source.document["bufferViews"].size();
        const auto accessor = skin_source.document["accessors"].size();
        const auto offset = skin_bytes->size();
        for (unsigned vertex = 0; vertex < 3; ++vertex)
            for (unsigned lane = 0; lane < 4; ++lane) {
                const auto bits = type == 5126 && lane == 0 ? std::bit_cast<unsigned>(1.f) : 0;
                for (unsigned byte = 0; byte < bytes_per_component; ++byte)
                    skin_bytes->push_back(std::byte((bits >> (byte * 8)) & 255));
            }
        skin_source.document["bufferViews"].push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", skin_bytes->size() - offset}});
        skin_source.document["accessors"].push_back(
            {{"bufferView", view}, {"componentType", type}, {"count", 3}, {"type", "VEC4"}});
        return accessor;
    };
    const auto joints = skin_attribute(5121, 1), weights = skin_attribute(5126, 4);
    skin_source.document["buffers"][0]["byteLength"] = skin_bytes->size();
    skin_source.buffers[0] = {skin_bytes, 0, skin_bytes->size()};
    skin_source.document["nodes"].push_back(Json::object());
    skin_source.document["nodes"].push_back(Json::object());
    skin_source.document["scenes"][0]["nodes"] = {0, 2, 3, 4};
    skin_source.document["skins"] = Json::array({{{"joints", {3}}}, {{"joints", {4}}}});
    skin_source.document["nodes"][0]["skin"] = 0;
    skin_source.document["nodes"][1]["skin"] = 0;
    for (auto& m : skin_source.document["meshes"])
        for (auto& p : m["primitives"]) {
            p["attributes"]["JOINTS_0"] = joints;
            p["attributes"]["WEIGHTS_0"] = weights;
        }
    auto skinned_files = cook_gltf_geometry_bundle(NativeGltfDocument(skin_source), {});
    (void)validate_model_bundle(skinned_files, ModelValidation::GeometryStage);
    const auto skinned_mesh = decode_mesh(file(skinned_files, "lod-0.fmesh").bytes);
    for (const auto& level : skinned_mesh.lods)
        for (const auto& part : level.parts)
            require(part.joint_palette == std::vector<std::uint32_t>{0},
                    "LOD geometry lost its validated skin palette");
    skin_source.document["nodes"][1]["skin"] = 1;
    rejects([&] { cook_gltf_geometry_bundle(NativeGltfDocument(skin_source), {}); });
    // The source uses replacement nodes: an animated owner cannot silently make
    // every alternative follow that animation. Unrelated node animation is fine.
    auto animated_source = source;
    const auto animated_data = gltf_instance_fixture(true);
    animated_source.buffers = animated_data.buffers;
    for (const auto* key : {"buffers", "bufferViews", "accessors", "animations"})
        animated_source.document[key] = animated_data.document.at(key);
    auto& channels = animated_source.document["animations"][0]["channels"];
    channels.erase(channels.begin()); // Keep the translation channel only.
    channels[0]["target"]["node"] = 2;
    require(
        gltf_mesh_lods(NativeGltfDocument(animated_source), gltf_scene_metadata(animated_source))
                .size() == 1,
        "Unrelated node animation incorrectly prevents LOD admission");
    for (unsigned target : {0u, 1u}) {
        channels[0]["target"]["node"] = target;
        rejects([&] {
            (void)gltf_mesh_lods(NativeGltfDocument(animated_source),
                                 gltf_scene_metadata(animated_source));
        });
    }
    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    rejects(
        [&] { cook_static_gltf_bundle(NativeGltfDocument(source), {}, cancelled.get_token()); });

    SubassetIdentityDocument previous{AssetId::generate(), source.source, source.source_digest,
                                      "forge.model.static.v1"};
    const auto first = reconcile_subassets(previous, source.source_digest, previous.evidence_schema,
                                           observations(index));
    require(first.document.has_value(), "Initial LOD identities failed");
    auto reordered = source;
    std::swap(reordered.document["meshes"][0], reordered.document["meshes"][1]);
    for (auto& node : reordered.document["nodes"])
        node["mesh"] = 1 - node["mesh"].template get<unsigned>();
    std::swap(reordered.document["nodes"][0], reordered.document["nodes"][1]);
    reordered.document["nodes"][1]["extensions"]["MSFT_lod"]["ids"] = {0};
    reordered.document["scenes"][0]["nodes"] = {1, 2};
    reordered.source_digest = asset_build_digest(reordered.document);
    const auto after = validate_model_bundle(cook(reordered));
    const auto matched = reconcile_subassets(*first.document, reordered.source_digest,
                                             previous.evidence_schema, observations(after));
    require(matched.document.has_value() &&
                first.assignments.at("/lods/0") == matched.assignments.at("/lods/1"),
            "Source reorder replaced the logical LOD mesh identity");
}
