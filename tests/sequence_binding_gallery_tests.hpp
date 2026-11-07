#pragma once
constexpr auto binding_gallery_scene = "Scenes/184-sequence-binding-crossfades.scene.json";
RuntimeUiInspector binding_ui_inspector(const std::filesystem::path& root,
                                        const std::filesystem::path& worker,
                                        const std::filesystem::path& font) {
    return [=](AssetId id, std::stop_token stop) {
        return inspect_ui_dependencies(worker, font, root, id, stop);
    };
}
void author_binding_sequence(const std::filesystem::path& root, const RuntimeUiInspector& inspect) {
    check(!std::filesystem::exists(root / binding_gallery_scene),
          "Binding sequence Gallery already exists");
    EngineContext engine;
    Scene scene(engine.world());
    const auto schema = scene.schema();
    const auto& types = schema.at("components");
    const auto declaration = std::find_if(types.begin(), types.end(), [](const auto& value) {
        return value.at("id") == "forge.ui_values";
    });
    check(declaration != types.end(), "UI Values declaration missing");
    const auto admission = declaration->at("admission");
    const auto slot = asset_detail::engine_mesh_resource(engine_primitive(0)).materials.front().key;
    const auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"windows", "d3d12", "desktop"});
    const auto publish = [&](const char* path, bool material, bool incoming) {
        auto document = TransformAnimationDocument::create(lease, path);
        auto source = document->source();
        source.retime_duration(2 * animation_timebase);
        if (material) {
            source.add_material_parameter(
                {slot, "baseColorFactor", MaterialParameterType::LinearColor4},
                incoming ? AnimationMaterialValues{.9, .3, .05, 1}
                         : AnimationMaterialValues{.1, .2, .9, 1});
            source.add_material_parameter({slot, "roughnessFactor", MaterialParameterType::Scalar},
                                          {incoming ? .9 : .1});
            if (incoming) {
                std::reverse(source.document["material_bindings"].begin(),
                             source.document["material_bindings"].end());
                for (auto& key : source.document["keys"])
                    std::reverse(key["material_values"].begin(), key["material_values"].end());
            }
        } else {
            for (const auto& [name, value] : std::array{std::pair{"amount", incoming ? .8 : .2},
                                                        std::pair{"width", incoming ? 220. : 80.}})
                source.add_component_field({"forge.ui_values",
                                            "values",
                                            admission,
                                            AnimationComponentValueType::Scalar,
                                            {},
                                            {Json{{"key", name}}, "value"}},
                                           value);
            if (incoming) {
                std::reverse(source.document["component_bindings"].begin(),
                             source.document["component_bindings"].end());
                for (auto& key : source.document["keys"])
                    std::reverse(key["component_values"].begin(), key["component_values"].end());
            }
        }
        source.document["channels"] = Json::array();
        document->edit(document->revision(), "Sequence binding clip",
                       [&](auto& value) { value = source.document; });
        document->save();
        importer.submit(
            importer.prepare(document->locator(), {}, source.asset()),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(importer.wait_idle(std::chrono::seconds(20)), "Binding Gallery clip cook timed out");
        const auto receipts = importer.poll();
        check(receipts.size() == 1 && receipts.front().published,
              "Binding Gallery clip publication failed");
        return source.asset();
    };
    const auto material_first =
        publish("Assets/Sequences/SurfaceFirst.animation.json", true, false);
    const auto material_next = publish("Assets/Sequences/SurfaceNext.animation.json", true, true);
    const auto field_first = publish("Assets/Sequences/FieldsFirst.animation.json", false, false);
    const auto field_next = publish("Assets/Sequences/FieldsNext.animation.json", false, true);
    auto source =
        duplicate_scene_asset(read(root / "Scenes/183-sequence-property-crossfades.scene.json"),
                              AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const auto& row) {
        return row.at("components").contains("forge.sprite_renderer");
    });
    std::string controller, cube;
    for (auto& row : source["entities"]) {
        auto& components = row["components"];
        if (components.contains("forge.light"))
            components.erase("forge.transform_animator");
        if (components.contains("forge.animation_sequence"))
            controller = row.at("id");
        if (row.at("name") == "Cube - animated lighting")
            cube = row.at("id");
    }
    check(!controller.empty() && !cube.empty(), "Binding Gallery base actors missing");
    scene.reset(source);
    scene.rename_entity(controller, "Four second binding loop");
    scene.rename_entity(cube, "Cube - color and roughness crossfade");
    scene.entity(cube).set<LocalTranslation>({0, 1, 0});
    const EntityRef reference{scene.asset_id(), EntityId::parse(controller)};
    const auto bind = [&](const std::string& id, AssetId first, AssetId next) {
        TransformAnimator track{{first}};
        track.sequence = reference;
        track.sequence_next_clip = {next};
        track.sequence_next_start_seconds = 1;
        scene.entity(id).set(track);
    };
    bind(cube, material_first, material_next);
    const auto hud =
        authoring_command(scene, "entity.create", {{"name", "HUD - reflected value crossfade"}})
            .at("selected")
            .get<std::string>();
    bind(hud, field_first, field_next);
    const auto ui_path = root / "Assets/UI/BindingCrossfade/hud.rml";
    std::filesystem::create_directories(ui_path.parent_path());
    asset_storage::replace(ui_path, R"rml(<rml><head><style>
body {width:100%;height:100%;margin:0;font-family:Lato;font-size:14px;color:#edf3fa;pointer-events:none;}
body,div,h1,p {display:block;}
#panel {position:absolute;right:3%;top:5%;width:250px;box-sizing:border-box;padding:12px;background-color:#182b3af2;}
h1 {font-size:18px;margin:0 0 8px;} p {margin:8px 0;}
.track {width:224px;height:20px;background-color:#101d29;overflow:hidden;}
#fill {height:20px;background-color:#48b6d5;} #fill.bright {background-color:#e9b75b;}
</style></head><body><div id="panel"><h1>Material and UI crossfades</h1>
<p>One sequence blends the cube and these named values.</p>
<div class="track"><div id="fill" data-style-width="width + 'px'" data-style-opacity="amount" data-class-bright="amount >= 0.6"></div></div>
<p id="amount">Opacity: {{amount}}</p><p id="width">Width: {{width}} px</p>
<p>The clips overlap from 1s to 2s. The four-second sequence repeats.</p>
</div></body></rml>)rml");
    const auto ui = register_ui_source(*lease, "Assets/UI/BindingCrossfade/hud.rml");
    scene.entity(hud).set<UiDocument>({{ui.id}}).set<UiValues>({{{"amount", .2}, {"width", 80.}}});
    scene.save(root / binding_gallery_scene);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(binding_gallery_scene);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, inspect, scene.schema());
}
void exercise_binding_sequence(const std::filesystem::path& root, AssetId scene_id) {
    const auto source_path = root / AssetCatalog::open_project(root).records().at(scene_id).source;
    const auto saved_file = read(source_path);
    const auto source = load_game_scene(root, {scene_id});
    EngineContext engine(WorldRole::Runtime, false,
                         {runtime_resources_module(root), animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (;;) {
        engine.services().resources()->synchronize();
        if (animation->prepare_initial_pose())
            break;
        check(std::chrono::steady_clock::now() < deadline, "Binding Gallery admission stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EntityRef controller;
    std::string cube, hud;
    for (const auto& row : source.at("entities")) {
        const auto& components = row.at("components");
        if (components.contains("forge.animation_sequence"))
            controller = {scene_id, row.at("id").get<EntityId>()};
        if (components.contains("forge.transform_animator") &&
            components.contains("forge.mesh_renderer"))
            cube = row.at("id");
        if (components.contains("forge.ui_values"))
            hud = row.at("id");
    }
    check(controller.entity && !cube.empty() && !hud.empty(), "Binding Gallery track missing");
    for (const double time : {0., .5, 1., 1.25, 1.5, 2., 3.5}) {
        AnimationPlaybackAccess::control(*animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        const double weight = std::clamp(time - 1, 0., 1.);
        const auto& renderer = scene.entity(cube).get<MeshRenderer>();
        near(parameter(renderer, "baseColorFactor", 0), .1 + .8 * weight);
        near(parameter(renderer, "baseColorFactor", 1), .2 + .1 * weight);
        near(parameter(renderer, "baseColorFactor", 2), .9 - .85 * weight);
        near(parameter(renderer, "baseColorFactor", 3), 1);
        near(parameter(renderer, "roughnessFactor", 0), .1 + .8 * weight);
        const auto& values = scene.entity(hud).get<UiValues>().values;
        check(values.size() == 2, "Binding Gallery resized its keyed values");
        for (const auto& value : values)
            near(value.value, value.name == "amount" ? .2 + .6 * weight : 80 + 140 * weight);
    }
    check(read(source_path) == saved_file, "Binding Gallery playback changed saved content");
}
void binding_sequence_export(const std::filesystem::path& gallery,
                             const std::filesystem::path& scratch,
                             const std::filesystem::path& worker,
                             const std::filesystem::path& font) {
    const auto work = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto source = work / "Project";
    std::filesystem::create_directories(work);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    const auto id = read(source / binding_gallery_scene).at("asset_id").get<AssetId>();
    const auto catalog = AssetCatalog::open_project(source);
    exercise_binding_sequence(source, id);
    const std::array roots{id};
    package_runtime_content(source, work / "Content", roots, {"windows", "d3d12"}, {}, {}, {},
                            binding_ui_inspector(source, worker, font));
    std::filesystem::rename(work / "Content", work / "Relocated");
    std::filesystem::rename(source, work / "source-offline");
    exercise_binding_sequence(work / "Relocated", id);
    const auto shipped = AssetCatalog::open_project(work / "Relocated");
    for (const auto& [asset, record] : shipped.records()) {
        (void)asset;
        if (record.type == TransformAnimationAsset::type)
            check(!std::filesystem::exists(work / "Relocated" / catalog.records().at(asset).source),
                  "Editable binding clip leaked into export");
    }
    std::cout << "Binding Gallery native Material/reflected values and relocated source-hidden "
                 "export passed\n";
}
