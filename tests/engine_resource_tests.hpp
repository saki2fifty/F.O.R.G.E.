#pragma once
#include "engine_render_resource.hpp"
#include <forge/geometry.hpp>
#include <forge/render_scene.hpp>
void check_engine_resources(const std::filesystem::path& root) {
    using namespace forge;
    using namespace forge::asset_detail;
    auto catalog = std::make_shared<AssetCatalog>(root);
    const auto before = catalog->document();
    std::set<AssetId> ids;
    for (const auto& asset : engine_assets()) {
        require(ids.insert(asset.id).second, "Engine identity collision");
        require(catalog->resolve(asset.id, asset.type).state == AssetState::Available,
                "Engine asset requires a project source file");
        require(catalog->resolve(asset.id, "scene").state == AssetState::Incompatible,
                "Engine asset type check bypassed");
        rejects([&] { catalog->add({asset.id, asset.type, "Assets/override", 1, {}}); });
        if (!asset.primitive) {
            (void)engine_material_resource({asset.id});
            continue;
        }
        const auto mesh = engine_mesh_resource({asset.id});
        const auto decoded = decode_mesh(encode_mesh(mesh.mesh));
        const auto& part = decoded.lods.at(0).parts.at(0);
        const auto& legacy = primitive_meshes().at(*asset.primitive);
        require(part.vertices == legacy.size(), "Built-in migration lost primitive vertices");
        const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& normals = std::get<std::vector<float>>(part.find("NORMAL")->values);
        require(part.find("TEXCOORD_0") && part.find("TANGENT"),
                "Engine shape cannot use textured or normal-mapped materials");
        const auto& uv = std::get<std::vector<float>>(part.find("TEXCOORD_0")->values);
        const auto& tangents = std::get<std::vector<float>>(part.find("TANGENT")->values);
        for (unsigned i = 0; i < part.vertices; ++i) {
            const Float3 n{normals[3 * i], normals[3 * i + 1], normals[3 * i + 2]},
                t{tangents[4 * i], tangents[4 * i + 1], tangents[4 * i + 2]};
            require(std::abs(geom_dot(n, t)) < .00001f && std::abs(geom_dot(t, t) - 1) < .00001f &&
                        std::abs(tangents[4 * i + 3]) == 1,
                    "Engine surface tangent is not an orthonormal direction/sign");
        }
        for (unsigned i = 0; i < part.vertices; i += 3) {
            auto point = [&](unsigned vertex) {
                return Float3{positions[3 * vertex], positions[3 * vertex + 1],
                              positions[3 * vertex + 2]};
            };
            const auto area =
                geom_cross(geom_sub(point(i + 1), point(i)), geom_sub(point(i + 2), point(i)));
            const Float3 normal{normals[3 * i], normals[3 * i + 1], normals[3 * i + 2]};
            require(geom_dot(area, normal) >= -1e-7f,
                    "Engine mesh winding disagrees with outward normals");
            const float du1 = uv[2 * (i + 1)] - uv[2 * i], du2 = uv[2 * (i + 2)] - uv[2 * i],
                        dv1 = uv[2 * (i + 1) + 1] - uv[2 * i + 1],
                        dv2 = uv[2 * (i + 2) + 1] - uv[2 * i + 1];
            if (geom_dot(area, area) > 1e-12f) {
                const auto determinant = du1 * dv2 - du2 * dv1;
                require(std::abs(determinant) > 1e-9f,
                        "Noncollapsed engine triangle has collapsed UV coordinates");
                if (determinant * tangents[4 * i + 3] <= 0)
                    throw std::runtime_error(
                        std::string("Engine tangent handedness disagrees with UVs: ") + asset.name +
                        " triangle " + std::to_string(i / 3));
            }
        }
        require(mesh.materials.at(0).key == "surface", "Engine material binding is not stable");
    }
    require(catalog->document() == before && catalog->records().empty(),
            "Engine assets polluted the project asset index");
    rejects([&] { engine_primitive(no_primitive); });
    rejects([&] { engine_primitive(primitive_count); });
    rejects([&] { engine_mesh_resource({engine_material().id}); });
    rejects([&] { engine_material_resource({engine_primitive(0).id}); });
    ResourcePool<MeshAsset> meshes;
    ResourcePool<MaterialAsset> materials;
    ResourcePool<TextureAsset> textures;
    ModelDrawCandidate candidate(root, catalog, 1, engine_primitive(0), {}, meshes);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (candidate.state() == ResourceState::Loading &&
           std::chrono::steady_clock::now() < deadline) {
        meshes.pump();
        materials.pump();
        textures.pump();
        candidate.advance(1, meshes, materials, textures);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(candidate.ready() && candidate.ready()->materials.size() == 1,
            "Engine assets did not use the complete async draw-resource path");
    const auto coalesced = request_engine_mesh(meshes, engine_primitive(0));
    require(meshes.acquire(coalesced).identity() == candidate.ready()->mesh.identity(),
            "Engine consumers duplicate the same CPU mesh revision");
    const auto entity = EntityId::generate();
    Json row{{"id", entity},
             {"spatial_resolved", true},
             {"world_affine", AffineTransform{}.m},
             {"components",
              {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
               {"forge.tint", {{"r", .1f}, {"g", .2f}, {"b", .3f}}}}}};
    Json document{{"asset_id", AssetId::generate()}, {"entities", {row}}};
    const auto original = document;
    auto extracted = extract_render_scene(document);
    require(extracted.meshes.size() == 1 &&
                extracted.meshes[0].renderer.mesh == engine_primitive(0) &&
                extracted.meshes[0].legacy_tint == std::array<float, 3>{.1f, .2f, .3f} &&
                document == original,
            "Legacy primitive adapter lost color/default cube or changed authored state");
    document["entities"][0]["components"]["forge.primitive"] = {{"kind", no_primitive}};
    require(extract_render_scene(document).meshes.empty(), "None primitive created an engine mesh");
    document["entities"][0]["components"]["forge.primitive"] = {{"kind", primitive_count}};
    require(extract_render_scene(document).diagnostics.at(0).category == "render.primitive.invalid",
            "Invalid primitive did not produce a structured diagnostic");
}
