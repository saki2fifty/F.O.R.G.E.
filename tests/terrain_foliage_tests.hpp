#pragma once
#include <limits>
void terrain_foliage(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    const auto lease = std::make_shared<ProjectLease>(root);
    auto document = TerrainDocument::create(lease, "Assets/Ground.terrain.json", [](AssetId id) {
        return TerrainSource::create(id, 3, 4);
    });
    document->edit(document->revision(), "Sloped ground", [](Json& json) {
        json["tiles"][0]["layers"][0]["heights"] = {0, 1, 2, .5, 1.5, 2.5, 1, 2, 3};
    });
    document->save();
    AssetImportService imports(lease, terrain_import_registry(), {"linux", "none", "cpu"});
    const auto id = document->source().asset();
    require(publish(imports, document->locator(), id).published, "Foliage terrain did not publish");
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto cooked = load(root, catalog, id);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name) {
        return authoring_command(scene, "entity.create",
                                 {{"recipe", "primitive.0"}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto surface = create("Ground");
    const auto prototype = create("Plant");
    authoring_command(scene, "property.set",
                      {{"entity", surface},
                       {"component", "forge.mesh_renderer"},
                       {"field", "mesh"},
                       {"value", id}});
    const auto rotation = rotation_about_axis({0, 1, 0}, 90);
    authoring_command(
        scene, "transform.local",
        {{"entity", surface},
         {"translation", {{"x", 10}, {"y", 3}, {"z", -4}}},
         {"rotation", {{"x", rotation.x}, {"y", rotation.y}, {"z", rotation.z}, {"w", rotation.w}}},
         {"scale", {{"x", 2}, {"y", 1.5}, {"z", 1}}}});
    const auto view = scene.effective_document();
    AffineTransform transform;
    for (const auto& row : view.at("entities"))
        if (row.at("id") == surface)
            transform.m = row.at("world_affine").get<std::array<double, 12>>();
    detail::MeshScatterSettings settings;
    settings.center = transform.point({2, 0, 2});
    settings.count = 24;
    settings.radius = .8;
    settings.spacing = .1;
    const auto arguments = detail::mesh_scatter_arguments(prototype, settings, "Terrain patch");
    const auto before = scene.document();
    const auto revision = scene.revision();
    const auto saved_source = read_bytes(root / document->locator(), terrain_source_byte_limit);
    const auto plan = prepare_terrain_foliage(*lease, scene, arguments, surface, .4);
    require(scene.document() == before && scene.revision() == revision &&
                read_bytes(root / document->locator(), terrain_source_byte_limit) == saved_source &&
                AssetCatalog::open_project(root).document() == catalog->document(),
            "Terrain preparation changed Scene/source/catalog intent");
    require(plan.at("scene_revision") == revision && plan.at("terrain_mesh") == Json(id) &&
                plan.at("terrain_revision") == selected_mesh_revision(*catalog, {id}),
            "Terrain preparation did not identify its exact Scene/publication");
    // Derive an independent world-plane oracle from the actual cooked vertices.
    const auto& part = cooked.lods.front().parts.front();
    const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
    const auto point = [&](unsigned i) {
        return transform.point({positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]});
    };
    const auto a = point(0), b = point(2), c = point(6);
    const Double3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const Double3 v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const Double3 normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                         u[0] * v[1] - u[1] * v[0]};
    const auto result = authoring_command(scene, "mesh.scatter", plan.at("arguments"));
    const auto after = scene.document();
    const auto effective = scene.effective_document();
    for (const auto& created : result.at("created")) {
        const Json* row = nullptr;
        for (const auto& candidate : effective.at("entities"))
            if (candidate.at("id") == created)
                row = &candidate;
        require(row, "Projected foliage entity missing");
        const auto& m = row->at("world_affine");
        const auto expected =
            a[1] -
            (normal[0] * (m[3].get<double>() - a[0]) + normal[2] * (m[11].get<double>() - a[2])) /
                normal[1] +
            .4;
        require(std::abs(m[7].get<double>() - expected) < 1e-9,
                "Foliage origins missed the transformed cooked heightfield surface");
    }
    require(scene.undo() && scene.document() == before && scene.redo() && scene.document() == after,
            "Terrain placement did not use one ordinary Scene history entry");
    const auto stable_revision = scene.revision();
    const auto cache_path = root / ".forge/cache/derived";
    const auto held_cache = root / "HeldCache";
    std::filesystem::rename(cache_path, held_cache);
    reject([&] { prepare_terrain_foliage(*lease, scene, arguments, surface); });
    require(!std::filesystem::exists(cache_path),
            "Read-only terrain preparation created a missing cache");
    std::filesystem::rename(held_cache, cache_path);
    reject([&] { prepare_terrain_foliage(*lease, scene, arguments, prototype); });
    for (const bool inverted : {false, true}) {
        EngineContext unsupported_engine;
        Scene unsupported(unsupported_engine.world());
        unsupported.reset(before);
        const auto q = rotation_about_axis({1, 0, 0}, inverted ? 0 : 15);
        authoring_command(unsupported, "transform.local",
                          {{"entity", surface},
                           {"rotation", {{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}}},
                           {"scale", {{"x", 1}, {"y", inverted ? -1 : 1}, {"z", 1}}}});
        const auto unchanged = unsupported.document();
        reject([&] { prepare_terrain_foliage(*lease, unsupported, arguments, surface); });
        require(unsupported.document() == unchanged,
                "Unsupported terrain transform changed the Scene");
    }
    auto outside = arguments;
    outside["center"]["x"] = 10000;
    reject([&] { prepare_terrain_foliage(*lease, scene, outside, surface); });
    auto stale_source = document->source().document;
    stale_source["tiles"][0]["layers"][0]["heights"][4] = 10;
    atomic_write(root / document->locator(), stale_source.dump());
    reject([&] { prepare_terrain_foliage(*lease, scene, arguments, surface); });
    atomic_write(
        root / document->locator(),
        std::string(reinterpret_cast<const char*>(saved_source.data()), saved_source.size()));
    require(scene.document() == after && scene.revision() == stable_revision &&
                AssetCatalog::open_project(root).document() == catalog->document(),
            "Rejected terrain preparation changed authored intent");
    const auto angle =
        std::atan2(std::hypot(normal[0], normal[2]), std::abs(normal[1])) * (180 / std::acos(-1.));
    const auto admitted = prepare_terrain_foliage(*lease, scene, arguments, surface, .4,
                                                  {angle - 1, angle + 1}, true);
    require(admitted.at("candidate_count") == settings.count &&
                admitted.at("retained_count") == settings.count,
            "Slope filter ignored actual yaw/nonuniform world scaling");
    require(admitted.at("aligned_to_slope") == true &&
                admitted.at("arguments").at("world_normals").size() == settings.count,
            "Terrain alignment did not supply original-order normals");
    const auto& prepared_normal = admitted.at("arguments").at("world_normals")[0];
    const Double3 direction{prepared_normal.at("x"), prepared_normal.at("y"),
                            prepared_normal.at("z")};
    const auto direction_length = std::hypot(direction[0], direction[1], direction[2]),
               oracle_length = std::hypot(normal[0], normal[1], normal[2]);
    const double sign = normal[1] < 0 ? -1 : 1;
    for (unsigned k = 0; k < 3; ++k)
        require(std::abs(direction[k] / direction_length - sign * normal[k] / oracle_length) <
                    1e-12,
                "Prepared alignment normal differs from transformed cooked geometry");
    reject([&] { prepare_terrain_foliage(*lease, scene, arguments, surface, .4, {0, angle - 1}); });
    for (const auto slopes : {TerrainFoliageSlopeRange{-1, 90},
                              {0, 91},
                              {40, 20},
                              {std::numeric_limits<double>::quiet_NaN(), 90}})
        reject([&] { prepare_terrain_foliage(*lease, scene, arguments, surface, .4, slopes); });
    require(scene.document() == after && scene.revision() == stable_revision,
            "Slope preflight/rejection changed Scene/history");
    EngineContext mirrored_engine;
    Scene mirrored(mirrored_engine.world());
    mirrored.reset(before);
    authoring_command(mirrored, "transform.local",
                      {{"entity", surface}, {"scale", {{"x", -2}, {"y", 1.5}, {"z", 1}}}});
    auto mirrored_arguments = arguments;
    const auto mirrored_view = mirrored.effective_document();
    for (const auto& row : mirrored_view.at("entities"))
        if (row.at("id") == surface) {
            AffineTransform affine;
            affine.m = row.at("world_affine").get<std::array<double, 12>>();
            const auto center = affine.point({2, 0, 2});
            mirrored_arguments["center"] = {{"x", center[0]}, {"y", center[1]}, {"z", center[2]}};
        }
    const auto mirrored_plan = prepare_terrain_foliage(*lease, mirrored, mirrored_arguments,
                                                       surface, .4, {angle - 1, angle + 1});
    require(mirrored_plan.at("retained_count") == settings.count,
            "Mirrored horizontal terrain inverted the upward geometric slope");
    document->edit(document->revision(), "Flat and steep halves", [](Json& json) {
        json["tiles"][0]["layers"][0]["heights"] = {0, 0, 4, 0, 0, 4, 0, 0, 4};
    });
    document->save();
    require(publish(imports, document->locator(), id).published,
            "Mixed slope terrain did not publish");
    const auto selected = prepare_terrain_foliage(*lease, scene, arguments, surface, .4, {0, 10});
    const auto layout = detail::mesh_scatter_layout(settings);
    const auto local_from_world = inverse(transform);
    unsigned expected_count = 0;
    for (unsigned i = 0; i < layout.size(); ++i) {
        const auto local =
            local_from_world.point({settings.center[0] + layout[i].offset[0], transform.m[7],
                                    settings.center[2] + layout[i].offset[1]});
        const bool flat = local[0] <= 2;
        require(selected.at("arguments").at("placement_mask")[i] == flat,
                "Slope mask differs from independent transformed flat/steep geometry");
        expected_count += flat;
    }
    require(expected_count > 0 && expected_count < settings.count &&
                selected.at("retained_count") == expected_count,
            "Slope filter did not retain a proper subset");
    const auto subset = authoring_command(scene, "mesh.scatter", selected.at("arguments"));
    const auto subset_doc = scene.document();
    require(subset.at("created").size() == expected_count && scene.undo() &&
                scene.document() == after && scene.redo() && scene.document() == subset_doc,
            "Slope subset count/history differs from prepared mask");
    std::cout << "Terrain foliage: published geometry, transformed heights, no preparation writes, "
                 "history and rejection passed\n";
}
