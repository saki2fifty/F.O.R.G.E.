#pragma once
inline void static_mesh_geometry_tests() {
    const auto id = AssetId::generate();
    auto doc = scene(false);
    auto& row = doc["entities"][0];
    row["components"].erase("forge.primitive");
    row["components"]["forge.mesh_renderer"] = {{"mesh", id}, {"enabled", true}};
    MeshResourceData data;
    auto& part = data.mesh.lods.emplace_back().parts.emplace_back();
    part.vertices = 4;
    part.streams = {{"POSITION", 3,
                     std::vector<float>{-.5f, 0, -.5f, -.5f, 0, .5f, .5f, 0, .5f, .5f, 0, -.5f}}};
    part.indices = {0, 1, 2, 0, 2, 3};
    part.bounds = mesh_bounds(part);
    auto capture_inputs = [&](const Sources& s) {
        return mesh_inputs(s, [&](AssetId mesh) {
            check(mesh == id, "Mesh identity changed during capture");
            return std::string(64, 'a');
        });
    };
    auto build = [&](const Json& source, const MeshResourceData& mesh) {
        auto s = capture(source);
        return geometry(s, capture_inputs(s),
                        [&](AssetId) -> const MeshResourceData& { return mesh; });
    };
    for (const auto scale :
         {LocalScale{20, 1, 20}, LocalScale{-20, 1, 20}, LocalScale{20, 0, 20}}) {
        row["world_affine"] = affine_transform({{}, {}, scale}).m;
        const auto g = build(doc, data);
        Query q(std::make_shared<Mesh>(build_tile(g, {})));
        check(q.path({-8, .1, 0}, {8, .1, 0}).status == NavStatus::Success,
              "Cooked Mesh winding lost walkable floor");
        auto meta = Json{{"version", 2},
                         {"geometry_version", 2},
                         {"asset_id", AssetId::generate()},
                         {"source_scene", g.scene},
                         {"sources", g.sources},
                         {"geometry_sha256", g.digest},
                         {"mesh_inputs", capture_inputs(capture(doc))},
                         {"settings", NavigationSettings{}}};
        const auto tile = build_tile(g, {});
        check(admit(envelope(meta, tile)).metadata.at("version") == 2,
              "Navigation v2 did not round trip");
        for (unsigned version : {0u, 1u, 3u}) {
            auto invalid = meta;
            invalid["version"] = version;
            rejects([&] { admit(envelope(invalid, tile)); });
        }
        for (const auto key : {"asset_id", "revision"}) {
            auto invalid = meta;
            invalid["mesh_inputs"][0].erase(key);
            rejects([&] { admit(envelope(invalid, tile)); });
        }
        auto duplicate = meta;
        duplicate["mesh_inputs"].push_back(duplicate["mesh_inputs"][0]);
        rejects([&] { admit(envelope(duplicate, tile)); });
        auto future = meta;
        future["geometry_version"] = 3;
        rejects([&] { admit(envelope(future, tile)); });
    }
    // Nonindexed triangles and multiple parts use the same forward geometry path.
    auto unindexed = data;
    auto& p = unindexed.mesh.lods[0].parts[0];
    p.vertices = 6;
    p.indices.clear();
    p.streams = {{"POSITION", 3,
                  std::vector<float>{-.5f, 0, -.5f, -.5f, 0, .5f, .5f, 0, .5f, -.5f, 0, -.5f, .5f,
                                     0, .5f, .5f, 0, -.5f}}};
    p.bounds = mesh_bounds(p);
    check(build(doc, unindexed).vertices == build(doc, data).vertices,
          "Sequential Mesh triangles differ from indexed input");
    auto multipart = data;
    multipart.mesh.lods[0].parts.push_back(part);
    check(build(doc, multipart).indices.size() == 12, "Mesh parts were silently omitted");
    auto bad = data;
    bad.mesh.lods[0].parts[0].indices[0] = 4;
    rejects([&] { build(doc, bad); });
    bad = data;
    bad.mesh.lods[0].parts[0].topology = MeshTopology::Lines;
    rejects([&] { build(doc, bad); });
    bad = data;
    bad.mesh.morph_names = {"change"};
    bad.mesh.morph_defaults = {0};
    rejects([&] { build(doc, bad); });
    bad = data;
    bad.mesh.lods[0].parts[0].joint_palette = {0};
    rejects([&] { build(doc, bad); });
    bad = data;
    bad.model.emplace();
    bad.model->nodes.push_back({AssetId::generate(), 0});
    rejects([&] { build(doc, bad); });
    bad = data;
    bad.mesh.lods[0].parts[0].streams[0].components = 2;
    rejects([&] { build(doc, bad); });
    bad = data;
    bad.mesh.lods[0].parts[0].indices.resize(49155, 0);
    rejects([&] { build(doc, bad); });
    auto moving = doc;
    moving["entities"][0]["components"]["forge.physics_body"] = {{"motion", 1}};
    rejects([&] { build(moving, data); });
    auto unresolved = doc;
    unresolved["entities"][0]["spatial_resolved"] = false;
    rejects([&] { build(unresolved, data); });
    auto group = doc;
    group["entities"][0]["components"].erase("forge.mesh_renderer");
    rejects([&] { capture(group); });
    auto disabled = doc;
    disabled["entities"][0]["components"]["forge.mesh_renderer"]["enabled"] = false;
    rejects([&] { capture(disabled); });
    auto too_many = doc;
    for (unsigned i = 0; i < 32; ++i) {
        auto next = row;
        next["id"] = EntityId::generate();
        next["components"]["forge.mesh_renderer"]["mesh"] = AssetId::generate();
        too_many["entities"].push_back(next);
    }
    rejects([&] { capture(too_many); });
    auto s = capture(doc);
    auto inputs = capture_inputs(s);
    const auto original = source_digest(s, inputs);
    auto changed = inputs;
    changed[0]["revision"] = std::string(64, 'b');
    check(source_digest(s, changed) != original,
          "Selected Mesh revision did not invalidate geometry");
    auto cosmetic = doc;
    cosmetic["entities"][0]["name"] = "Rename";
    check(source_digest(capture(cosmetic), inputs) == original,
          "Cosmetic name invalidated Mesh navigation");
}
