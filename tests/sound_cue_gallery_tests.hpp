#pragma once
// Reuses the actual audio Gallery fixture and package owners in the enclosing TU.
struct SoundCueGalleryProfile {
    const char* scene;
    const char* id;
    bool timed;
    bool repeating = false;
    bool completion = false;
};
constexpr SoundCueGalleryProfile event_sound_cue_gallery{
    "Scenes/192-event-sound-cues.scene.json", "7067a5b8-251b-4b35-93b3-0fdf7871178b", false};
constexpr SoundCueGalleryProfile timed_sound_cue_gallery{
    "Scenes/193-timed-sound-cues.scene.json", "9c91c46a-2d17-4e81-a7d3-18343e332d1d", true};

constexpr SoundCueGalleryProfile repeating_sound_cue_gallery{
    "Scenes/194-repeating-sound-cues.scene.json", "7de36cf2-3ce5-4894-9e01-0b1c1e4b16bd", true,
    true};

constexpr SoundCueGalleryProfile completion_sound_cue_gallery{
    "Scenes/195-completion-sound-cues.scene.json", "fc1ab5b9-221b-4922-930b-489357d1b483", true,
    true, true};

void author_sound_cue_gallery(const std::filesystem::path& root,
                              const SoundCueGalleryProfile& profile = event_sound_cue_gallery) {
    require(!std::filesystem::exists(root / profile.scene), "Sound Cue Gallery already exists");
    ProjectLease lease(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto document = duplicate_scene_asset(read(root / "Scenes/33-audio-mixer.scene.json"),
                                          AssetId::parse(profile.id), scene.schema());
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
        if (profile.repeating)
            value["pause_with_game"] = false;
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
    SoundCue definition{true,
                        17,
                        0,
                        {{1, {}, {{1, 1}, {2, 1}}},
                         {0, scene.reference(sources[0]), {}},
                         {3, {}, {{3, 1}, {4, 1}}},
                         {0, scene.reference(sources[1]), {}},
                         {2, {}, {{5, 3}, {6, 1}}},
                         {0, scene.reference(sources[2]), {}},
                         {0, scene.reference(sources[1]), {}}}};
    if (profile.timed) {
        definition.nodes[3].delay_seconds = .128;
        definition.nodes[5].delay_seconds = .256;
        definition.nodes[6].delay_seconds = .384;
    }
    if (profile.repeating) {
        definition.repeat_interval_seconds = profile.completion ? 0 : .512;
        definition.repeat_mode = profile.completion ? 1 : 0;
        definition.repeat_on_start = true;
        definition.repeat_pause_with_game = false;
    }
    const auto cue = create(profile.completion ? "Completion layered events - select for Live sound"
                            : profile.repeating ? "Repeating layered events - select for Live sound"
                            : profile.timed     ? "Timed layered event - select for Trigger cue"
                                                : "Layered event - select for Trigger cue",
                            "audio.sound_cue", sound_cue_json(definition));
    if (!profile.repeating) {
        const auto controller = create("Two-second sound events", "animation.sequence",
                                       {{"duration", 2}, {"loop", true}});
        for (unsigned i = 0; i < 4; ++i)
            create(("Event " + std::to_string(i + 1)).c_str(), "animation.audio_cue",
                   {{"sequence", scene.reference(controller)},
                    {"source", scene.reference(cue)},
                    {"time_seconds", i * .5}});
    }
    scene.save(root / profile.scene);
    auto catalog = AssetCatalog::open_project(root);
    (void)catalog.add_scene(profile.scene);
    catalog.save(AssetCatalog::project_index(root));
}

void verify_sound_cue_gallery(const std::filesystem::path& gallery,
                              const std::filesystem::path& scratch,
                              const SoundCueGalleryProfile& profile = event_sound_cue_gallery) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto source = root / "source", package = root / "package", moved = root / "relocated";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    if (!std::filesystem::exists(source / profile.scene))
        author_sound_cue_gallery(source, profile);
    const auto document = read(source / profile.scene);
    require(document.at("asset_id") == profile.id, "Sound Cue example changed its Scene identity");
    EngineContext inspector;
    Scene schema_scene(inspector.world());
    const auto catalog = AssetCatalog::open_project(source);
    const auto id = document.at("asset_id").get<AssetId>();
    require(catalog.records().at(id).source == profile.scene,
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
    Json kit{{"runtime_sound_cues", sound_cue_profile_version}};
    const auto& features =
        prepared_scene.metadata.at("forge.runtime_document").at("runtime_features");
    require(features.contains("sound_cue_timing") == profile.timed,
            "Actual prepared Scene has incorrect timed cue requirements");
    if (profile.timed) {
        bool rejected = false;
        try {
            validate_runtime_compatibility(selected, kit);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Timed cues admitted a kit supporting only earlier event cues");
        kit["runtime_sound_cue_timing"] = sound_cue_timing_profile_version;
    }
    require(features.contains("sound_cue_repeat") == profile.repeating,
            "Actual prepared Scene has incorrect repeating cue requirements");
    if (profile.repeating) {
        bool rejected = false;
        try {
            validate_runtime_compatibility(selected, kit);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Repeating events admitted an earlier event/timing-only kit");
        kit["runtime_sound_cue_repeat"] = sound_cue_repeat_profile_version;
    }
    require(features.contains("sound_cue_completion") == profile.completion,
            "Actual prepared Scene has incorrect completion cue requirements");
    if (profile.completion) {
        bool rejected = false;
        try {
            validate_runtime_compatibility(selected, kit);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Completion events admitted an older periodic-only kit");
        kit["runtime_sound_cue_completion"] = sound_cue_completion_profile_version;
    }
    validate_runtime_compatibility(selected, kit);
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
    require(graph.nodes[1].delay_seconds == 0 &&
                graph.nodes[3].delay_seconds == (profile.timed ? .128 : 0) &&
                graph.nodes[5].delay_seconds == (profile.timed ? .256 : 0) &&
                graph.nodes[6].delay_seconds == (profile.timed ? .384 : 0),
            "Gallery Source offsets differ from the independent sample deadlines");
    const auto routing =
        audio_routing_settings(ProjectSettings(source).document().at("game").at("audio"));
    auto oracle_document = manual;
    if (profile.repeating)
        for (auto& row : oracle_document["entities"])
            if (row["components"].contains("forge.sound_cue"))
                row["components"]["forge.sound_cue"]["repeat_on_start"] = false;
    require(graph.repeat_interval_seconds == (profile.completion  ? 0
                                              : profile.repeating ? .512
                                                                  : 0) &&
                graph.repeat_mode == (profile.completion ? 1u : 0u) &&
                graph.repeat_on_start == profile.repeating &&
                graph.repeat_pause_with_game != profile.repeating,
            "Gallery repeating configuration changed");
    Game authored(source, manual, routing), oracle(source, oracle_document, routing);
    std::filesystem::rename(source, root / "source-hidden");
    Game exported(moved, cooked, routing);
    if (profile.repeating)
        for (auto* game : {&authored, &exported, &oracle})
            game->audio->game_paused(true);
    const auto original_tick = exported.audio->status().at("tick");
    const auto authored_before = authored.scene.snapshot(),
               exported_before = exported.scene.snapshot();
    double energy = 0;
    std::uint64_t frame = 0;
    std::multimap<std::uint64_t, EntityRef> planned;
    const auto quantum = [&] {
        while (!planned.empty() && planned.begin()->first <= frame) {
            AudioSourceAccess::control(*oracle.audio, planned.begin()->second,
                                       AudioSourceOperation::OneShot);
            planned.erase(planned.begin());
        }
        std::array<float, 512> a{}, b{}, expected{};
        authored.audio->read_offline(a);
        exported.audio->read_offline(b);
        oracle.audio->read_offline(expected);
        require(a == b && a == expected,
                "Cue source-hidden PCM differs from independently timed ordinary leaf requests");
        for (float value : b) {
            require(std::isfinite(value), "Cue Gallery produced nonfinite PCM");
            energy += double(value) * value;
        }
        frame += 256;
    };
    for (unsigned event = 0; event < 12; ++event) {
        // Closed-form oracle for this fixed example, without production graph traversal/state.
        unsigned selected_index = 3;
        if (event % 2) {
            std::seed_seq seeds{17u, 4u, event / 2, 0u};
            std::mt19937 random(seeds);
            const auto ticket = (std::uint64_t(random()) * 4) >> 32;
            selected_index = ticket < 3 ? 5 : 6;
        }
        const auto selected = graph.nodes[selected_index].source;
        const auto offset = profile.timed ? (selected_index == 3   ? 6144u
                                             : selected_index == 5 ? 12288u
                                                                   : 18432u)
                                          : 0u;
        if (!profile.repeating)
            for (auto* game : {&authored, &exported})
                AudioSourceAccess::control(*game->audio, game->scene.reference(cue),
                                           AudioSourceOperation::OneShot);
        // Independent fixed sample deadlines; no production selection or timer helper.
        planned.emplace(frame, graph.nodes[1].source);
        planned.emplace(frame + offset, selected);
        if (profile.completion) {
            unsigned quanta = 0;
            do {
                require(++quanta <= 8192,
                        "Completion Gallery event failed to retire within its bound");
                quantum();
            } while (
                !planned.empty() ||
                AudioSourceAccess::status(*oracle.audio, graph.nodes[1].source).at("one_shots") !=
                    0 ||
                AudioSourceAccess::status(*oracle.audio, selected).at("one_shots") != 0);
        } else {
            for (unsigned i = 0; i < (profile.repeating ? 96u : 32u); ++i)
                quantum();
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
    if (profile.repeating) {
        for (auto* game : {&authored, &exported})
            AudioSourceAccess::control(*game->audio, game->scene.reference(cue),
                                       AudioSourceOperation::Stop);
        require(exported.audio->status().at("tick") == original_tick,
                "Independent Gallery repetition required game ticks");
    }
    for (unsigned i = 0; profile.timed && i < 72; ++i)
        quantum();
    require(planned.empty(), "Cue timing oracle left unheard scheduled onsets");
    require(energy > 1e-5 && authored.scene.snapshot() == authored_before &&
                exported.scene.snapshot() == exported_before,
            "Cue Gallery was silent or triggering changed saved component data");
    std::cout << "Original Sound Cue Gallery:12 closed-form event pairs, exact independent PCM and "
                 "source-hidden moved cooked content passed\n";
    const auto proved = std::filesystem::absolute(scratch) /
                        (profile.completion  ? "proved-completion-sound-cue.scene.json"
                         : profile.repeating ? "proved-repeating-sound-cue.scene.json"
                         : profile.timed     ? "proved-timed-sound-cue.scene.json"
                                             : "proved-sound-cue.scene.json");
    std::filesystem::copy_file(root / "source-hidden" / profile.scene, proved,
                               std::filesystem::copy_options::overwrite_existing);
    std::cout << "Proved source Scene retained at " << proved << '\n';
    std::filesystem::remove_all(root);
}

void publish_sound_cue_gallery(const std::filesystem::path& proof,
                               const std::filesystem::path& root,
                               const SoundCueGalleryProfile& profile = event_sound_cue_gallery) {
    require(!std::filesystem::exists(root / profile.scene), "Sound Cue Gallery already exists");
    const auto document = read(proof);
    require(document.at("asset_id") == profile.id, "Proved Scene has the wrong example identity");
    EngineContext validation;
    Scene scene(validation.world());
    scene.reset(document);
    ProjectLease lease(root);
    auto catalog = AssetCatalog::open_project(root);
    require(!catalog.records().contains(AssetId::parse(profile.id)),
            "Sound Cue Scene identity is already registered");
    std::filesystem::copy_file(proof, root / profile.scene);
    (void)catalog.add_scene(profile.scene);
    catalog.save(AssetCatalog::project_index(root));
}
