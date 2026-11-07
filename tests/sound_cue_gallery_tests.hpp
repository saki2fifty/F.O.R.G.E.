#pragma once
// Reuses the actual audio Gallery fixture and package owners in the enclosing TU.
constexpr auto sound_cue_example = "Scenes/192-event-sound-cues.scene.json";
constexpr auto sound_cue_example_id = "7067a5b8-251b-4b35-93b3-0fdf7871178b";

void author_sound_cue_gallery(const std::filesystem::path& root) {
    require(!std::filesystem::exists(root / sound_cue_example), "Sound Cue Gallery already exists");
    ProjectLease lease(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto document = duplicate_scene_asset(read(root / "Scenes/33-audio-mixer.scene.json"),
                                          AssetId::parse(sound_cue_example_id), scene.schema());
    std::array<std::string, 3> sources;
    for (auto& row : document["entities"]) {
        auto& components = row["components"];
        if (!components.contains("forge.audio_source"))
            continue;
        auto& value = components["forge.audio_source"];
        const auto index = value.at("bus").get<unsigned>() - 1;
        require(index < 3, "Original Gallery tone has an unknown stock bus");
        sources[index] = row.at("id");
        value["play_on_start"] = false;
        value["loop"] = false;
        value["gain"] = .25f;
        value["custom_bus"] = "";
        row["name"] = index == 0   ? "Base tone - every event"
                      : index == 1 ? "Middle tone - ordered and weighted"
                                   : "High tone - weighted choice";
    }
    scene.reset(document);
    const auto create = [&](const char* name, const char* recipe, const Json& values) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    const SoundCue definition{true,
                              17,
                              0,
                              {{1, {}, {{1, 1}, {2, 1}}},
                               {0, scene.reference(sources[0]), {}},
                               {3, {}, {{3, 1}, {4, 1}}},
                               {0, scene.reference(sources[1]), {}},
                               {2, {}, {{5, 3}, {6, 1}}},
                               {0, scene.reference(sources[2]), {}},
                               {0, scene.reference(sources[1]), {}}}};
    const auto cue = create("Layered event - select for Trigger cue", "audio.sound_cue",
                            sound_cue_json(definition));
    const auto controller =
        create("Two-second sound events", "animation.sequence", {{"duration", 2}, {"loop", true}});
    for (unsigned i = 0; i < 4; ++i)
        create(("Event " + std::to_string(i + 1)).c_str(), "animation.audio_cue",
               {{"sequence", scene.reference(controller)},
                {"source", scene.reference(cue)},
                {"time_seconds", i * .5}});
    scene.save(root / sound_cue_example);
    auto catalog = AssetCatalog::open_project(root);
    (void)catalog.add_scene(sound_cue_example);
    catalog.save(AssetCatalog::project_index(root));
}

void verify_sound_cue_gallery(const std::filesystem::path& gallery,
                              const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto source = root / "source", package = root / "package", moved = root / "relocated";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    if (!std::filesystem::exists(source / sound_cue_example))
        author_sound_cue_gallery(source);
    const auto document = read(source / sound_cue_example);
    require(document.at("asset_id") == sound_cue_example_id,
            "Sound Cue example changed its Scene identity");
    EngineContext inspector;
    Scene schema_scene(inspector.world());
    const auto catalog = AssetCatalog::open_project(source);
    const auto id = document.at("asset_id").get<AssetId>();
    require(catalog.records().at(id).source == sound_cue_example,
            "Sound Cue Gallery is not registered");
    const std::array roots{id};
    {
        ProjectLease lease(source);
        prepare_runtime_content_catalog(lease, roots, {}, schema_scene.schema());
    }
    const RuntimePackageTarget target{"windows", "d3d12"};
    package_runtime_content(source, package, roots, target);
    std::filesystem::rename(package, moved);
    const auto packaged = open_runtime_content(moved, target);
    // Document-edge metadata belongs to the detached/runtime catalog, not the authoring index.
    const auto& prepared_scene = packaged.records().at(id);
    require(prepared_scene.metadata.at("forge.runtime_document")
                    .at("runtime_features")
                    .at("sound_cues") == sound_cue_profile_version,
            "Actual cooked Scene lost its Sound Cue runtime requirement");
    AssetCatalog selected(source);
    selected.add(prepared_scene);
    bool old_kit_rejected = false;
    try {
        validate_runtime_compatibility(selected, Json::object());
    } catch (const std::exception&) {
        old_kit_rejected = true;
    }
    require(old_kit_rejected, "Actual prepared cue Scene admitted an older runtime kit");
    validate_runtime_compatibility(selected, {{"runtime_sound_cues", sound_cue_profile_version}});
    auto cooked = read(moved / packaged.records().at(id).source);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(moved))
        require(entry.path().extension() != ".wav",
                "Sound Cue package retained its source waveform");
    auto manual = document;
    std::string cue;
    SoundCue graph;
    for (auto& row : manual["entities"]) {
        auto& values = row["components"];
        if (values.contains("forge.sound_cue")) {
            cue = row.at("id");
            graph = sound_cue_from_json(values.at("forge.sound_cue"));
        }
        if (values.contains("forge.animation_sequence"))
            values["forge.animation_sequence"]["play_on_start"] = false;
    }
    for (auto& row : cooked["entities"])
        if (row["components"].contains("forge.animation_sequence"))
            row["components"]["forge.animation_sequence"]["play_on_start"] = false;
    require(graph.nodes.size() == 7 && graph.nodes[0].kind == 1 && graph.nodes[2].kind == 3 &&
                graph.nodes[4].kind == 2,
            "Gallery omitted Layer/Ordered/Weighted composition");
    const auto routing =
        audio_routing_settings(ProjectSettings(source).document().at("game").at("audio"));
    Game authored(source, manual, routing), oracle(source, manual, routing);
    std::filesystem::rename(source, root / "source-hidden");
    Game exported(moved, cooked, routing);
    const auto authored_before = authored.scene.snapshot(),
               exported_before = exported.scene.snapshot();
    double energy = 0;
    for (unsigned event = 0; event < 12; ++event) {
        // Closed-form oracle for this fixed example, without production graph traversal/state.
        auto selected = graph.nodes[3].source;
        if (event % 2) {
            std::seed_seq seeds{17u, 4u, event / 2, 0u};
            std::mt19937 random(seeds);
            const auto ticket = (std::uint64_t(random()) * 4) >> 32;
            selected = graph.nodes[ticket < 3 ? 5 : 6].source;
        }
        for (auto* game : {&authored, &exported})
            AudioSourceAccess::control(*game->audio, game->scene.reference(cue),
                                       AudioSourceOperation::OneShot);
        for (const auto leaf : {graph.nodes[1].source, selected})
            AudioSourceAccess::control(*oracle.audio, leaf, AudioSourceOperation::OneShot);
        for (unsigned quantum = 0; quantum < 32; ++quantum) {
            std::array<float, 512> a{}, b{}, expected{};
            authored.audio->read_offline(a);
            exported.audio->read_offline(b);
            oracle.audio->read_offline(expected);
            require(a == b && a == expected,
                    "Cue source-hidden PCM differs from explicit independent leaf requests");
            for (float value : b) {
                require(std::isfinite(value), "Cue Gallery produced nonfinite PCM");
                energy += double(value) * value;
            }
        }
        const auto status =
            AudioSourceAccess::status(*exported.audio, exported.scene.reference(cue));
        require(status.at("accepted_triggers") == event + 1 &&
                    status.at("last_sources") == Json::array({graph.nodes[1].source, selected}),
                "Cue Gallery selected a different ordered/weighted leaf pair");
        require(exported.audio->status().at("source_voices") == 3 &&
                    exported.audio->status().at("clips") == 1,
                "Cue Gallery introduced another primary voice or duplicated the shared clip");
    }
    require(energy > 1e-5 && authored.scene.snapshot() == authored_before &&
                exported.scene.snapshot() == exported_before,
            "Cue Gallery was silent or triggering changed saved component data");
    std::cout << "Original Sound Cue Gallery:12 closed-form event pairs, exact independent PCM and "
                 "source-hidden moved cooked content passed\n";
    const auto proved = std::filesystem::absolute(scratch) / "proved-sound-cue.scene.json";
    std::filesystem::copy_file(root / "source-hidden" / sound_cue_example, proved,
                               std::filesystem::copy_options::overwrite_existing);
    std::cout << "Proved source Scene retained at " << proved << '\n';
    std::filesystem::remove_all(root);
}

void publish_sound_cue_gallery(const std::filesystem::path& proof,
                               const std::filesystem::path& root) {
    require(!std::filesystem::exists(root / sound_cue_example), "Sound Cue Gallery already exists");
    const auto document = read(proof);
    require(document.at("asset_id") == sound_cue_example_id,
            "Proved Scene has the wrong example identity");
    EngineContext validation;
    Scene scene(validation.world());
    scene.reset(document);
    ProjectLease lease(root);
    auto catalog = AssetCatalog::open_project(root);
    require(!catalog.records().contains(AssetId::parse(sound_cue_example_id)),
            "Sound Cue Scene identity is already registered");
    std::filesystem::copy_file(proof, root / sound_cue_example);
    (void)catalog.add_scene(sound_cue_example);
    catalog.save(AssetCatalog::project_index(root));
}
