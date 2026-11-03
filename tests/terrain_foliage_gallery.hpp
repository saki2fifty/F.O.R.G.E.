#pragma once
constexpr auto terrain_scene_path = "Scenes/147-terrain-foliage.scene.json";
void author_terrain_meadow(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / terrain_scene_path),
          "Terrain foliage Gallery already exists");
    const auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto source =
        duplicate_scene_asset(read(root / "Scenes/55-layered-heightfield-terrain.scene.json"),
                              AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        const auto& c = row.at("components");
        return !c.contains("forge.camera") && !c.contains("forge.light") &&
               !row.at("name").get<std::string>().starts_with("Layered terrain");
    });
    std::string surface;
    for (auto& row : source["entities"])
        if (row.at("components").contains("forge.mesh_renderer")) {
            surface = row.at("id");
            row["components"].erase("forge.physics_body");
            row["components"].erase("forge.asset_collider");
        }
    scene.reset(source);
    Json renderer;
    const auto meadow = read(root / scene_path);
    for (const auto& row : meadow.at("entities"))
        if (row.at("name").get<std::string>().starts_with("Green meadow ")) {
            renderer = row.at("components").at("forge.mesh_renderer");
            break;
        }
    check(!renderer.is_null() && !surface.empty(), "Terrain meadow template/surface is missing");
    const auto prototype =
        authoring_command(scene, "entity.create",
                          {{"recipe", "render.mesh"},
                           {"name", "Blade template - select for terrain placement"},
                           {"position", {{"x", -7}, {"y", 0}, {"z", -4}}},
                           {"component_values", renderer}})
            .at("selected")
            .get<std::string>();
    detail::MeshScatterSettings settings;
    settings.center = {-7, 0, -4};
    settings.radius = .01;
    settings.count = 1;
    settings.spacing = 0;
    const auto sample = prepare_terrain_foliage(
        *lease, scene, detail::mesh_scatter_arguments(prototype, settings, "Template sample"),
        surface);
    const auto placement = detail::mesh_scatter_layout(settings).front();
    authoring_command(scene, "transform.position",
                      {{"entity", prototype},
                       {"value",
                        {{"x", settings.center[0] + placement.offset[0]},
                         {"y", sample.at("arguments").at("world_heights")[0]},
                         {"z", settings.center[2] + placement.offset[1]}}}});
    settings.center = {0, 0, 0};
    settings.radius = 5;
    settings.count = 128;
    settings.spacing = .55;
    settings.seed = 91;
    settings.scale_min = .65;
    settings.scale_max = 1.15;
    const auto plan = prepare_terrain_foliage(
        *lease, scene, detail::mesh_scatter_arguments(prototype, settings, "Terrain meadow"),
        surface);
    apply_authoring(
        scene, Json::array({{{"operation", "mesh.scatter"}, {"arguments", plan.at("arguments")}}}),
        plan.at("scene_revision"));
    scene.save(root / terrain_scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(terrain_scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
Json terrain_meadow_oracle(const std::filesystem::path& root) {
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(read(root / terrain_scene_path));
    const auto view = scene.effective_document();
    Double3 ground{};
    AssetId mesh;
    for (const auto& row : view.at("entities"))
        if (row.at("name").get<std::string>().starts_with("Layered terrain")) {
            const auto& m = row.at("world_affine");
            ground = {m[3], m[7], m[11]};
            mesh = row.at("components").at("forge.mesh_renderer").at("mesh").get<AssetId>();
        }
    check(bool(mesh), "Terrain meadow surface is missing");
    const auto catalog = AssetCatalog::open_project(root);
    const TerrainSource source{read(root / catalog.records().at(mesh).source)};
    const auto raster = source.raster();
    Json result = Json::object();
    for (const auto& row : view.at("entities"))
        if (row.at("name").get<std::string>().starts_with("Terrain meadow ")) {
            const auto& m = row.at("world_affine");
            const auto height = terrain_height(raster, m[3].get<double>() - ground[0],
                                               m[11].get<double>() - ground[2]);
            check(height.has_value(), "Terrain meadow origin has no heightfield coverage");
            result[row.at("id").get<std::string>()] = {m[3], ground[1] + *height, m[11]};
        }
    check(result.size() == 128, "Terrain meadow lost ordinary placements");
    return result;
}
