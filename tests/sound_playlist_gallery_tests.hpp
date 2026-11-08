#pragma once
#include <forge/game_content.hpp>
#include <forge/prefab_authoring.hpp>

constexpr auto playlist_scene_path = "Scenes/198-ordered-sound-playlist.scene.json";
constexpr auto playlist_prefab_path = "Assets/Audio/ThreeTonePlaylist.prefab.json";
constexpr auto playlist_scene_id = "abfc1e8a-5226-412b-9029-1e568b4adf1a";

void author_sound_playlist_gallery(const std::filesystem::path& root) {
    require(!std::filesystem::exists(root / playlist_scene_path) &&
                !std::filesystem::exists(root / playlist_prefab_path),
            "Playlist Gallery already exists");
    ProjectLease lease(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto document = duplicate_scene_asset(read(root / "Scenes/33-audio-mixer.scene.json"),
                                          AssetId::parse(playlist_scene_id), scene.schema());
    AssetId clip;
    for (auto& row : document["entities"])
        if (row["components"].contains("forge.audio_source")) {
            clip = row["components"]["forge.audio_source"].at("clip").get<AssetId>();
            row["components"].erase("forge.audio_source");
        }
    require(bool(clip), "Original Gallery tone is missing");
    scene.reset(document);
    const auto created =
        authoring_command(scene, "audio.playlist_create",
                          {{"name", "Three-tone playlist - select for Live sound"},
                           {"clips", Json::array({{{"clip", clip}, {"name", "Low tone"}},
                                                  {{"clip", clip}, {"name", "Middle tone"}},
                                                  {{"clip", clip}, {"name", "High tone"}}})}});
    const auto sources = created.at("sources").get<std::vector<std::string>>();
    const std::array pitches{.75f, 1.f, 1.5f};
    for (unsigned i = 0; i < sources.size(); ++i)
        authoring_command(scene, "property.set_many",
                          {{"entity", sources[i]},
                           {"component", "forge.audio_source"},
                           {"values", {{"pitch", pitches[i]}, {"gain", .25f}}}});
    const auto original = created.at("selected").get<std::string>();
    auto captured = create_prefab_source(scene, original);
    PrefabLibrary prefabs(root);
    const auto prefab = prefabs.create(scene, captured, playlist_prefab_path);
    (void)instantiate_prefab(scene, prefab);
    scene.delete_subtree(original);
    scene.save(root / playlist_scene_path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add(prefabs.records().at(prefab));
    (void)catalog.add_scene(playlist_scene_path);
    catalog.save(AssetCatalog::project_index(root));
}

void verify_sound_playlist_gallery(const std::filesystem::path& gallery,
                                   const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto source = root / "source", package = root / "package", moved = root / "relocated";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    if (!std::filesystem::exists(source / playlist_scene_path))
        author_sound_playlist_gallery(source);
    const auto id = AssetId::parse(playlist_scene_id);
    const auto document = load_game_scene(source, {id});
    const auto routing =
        audio_routing_settings(ProjectSettings(source).document().at("game").at("audio"));
    Game authored(source, document, routing);
    const auto effective = authored.scene.effective_document();
    std::string cue;
    SoundCue graph;
    for (const auto& row : effective.at("entities"))
        if (row.at("components").contains("forge.sound_cue")) {
            require(cue.empty(), "Playlist example contains more than one autoplay cue");
            cue = row.at("id");
            graph = sound_cue_from_json(row.at("components").at("forge.sound_cue"));
        }
    require(!cue.empty() && graph.nodes.size() == 4 && graph.nodes[0].kind == 3 &&
                graph.repeat_mode == 1 && graph.repeat_event_limit == 3 && graph.repeat_on_start,
            "Playlist example lost its ordinary finite Ordered composition");
    require(document.at("_prefab_sources").size() == 1,
            "Playlist example did not resolve its portable Prefab");
    Json oracle_document{{"version", 3}, {"asset_id", id}, {"entities", Json::array()}};
    for (unsigned i = 1; i < graph.nodes.size(); ++i) {
        const auto entity = graph.nodes[i].source.entity.str();
        auto found = std::find_if(effective.at("entities").begin(), effective.at("entities").end(),
                                  [&](const Json& row) { return row.at("id") == entity; });
        require(found != effective.at("entities").end(),
                "Playlist Prefab retained an external Source");
        oracle_document["entities"].push_back(
            {{"id", entity},
             {"name", "Independent ordinary Source"},
             {"components",
              {{"forge.audio_source", found->at("components").at("forge.audio_source")}}}});
    }
    Game oracle(source, oracle_document, routing);
    const std::array roots{id};
    {
        ProjectLease lease(source);
        prepare_runtime_content_catalog(lease, roots, {}, authored.scene.schema());
    }
    const RuntimePackageTarget target{"windows", "d3d12"};
    package_runtime_content(source, package, roots, target);
    std::filesystem::rename(package, moved);
    const auto packaged = open_runtime_content(moved, target);
    const auto source_catalog = AssetCatalog::open_project(source);
    const auto prefab_record =
        std::find_if(source_catalog.records().begin(), source_catalog.records().end(),
                     [](const auto& entry) { return entry.second.source == playlist_prefab_path; });
    require(prefab_record != source_catalog.records().end() &&
                packaged.records().contains(prefab_record->first),
            "Playlist export omitted its actual Prefab dependency");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(moved))
        require(entry.path().extension() != ".wav", "Playlist export retained source audio");
    std::filesystem::rename(source, root / "source-hidden");
    Game exported(moved, load_game_scene(moved, {id}), routing);
    const auto authored_before = authored.scene.snapshot(),
               exported_before = exported.scene.snapshot();
    double energy = 0;
    for (unsigned event = 0; event < 3; ++event) {
        const auto leaf = graph.nodes[event + 1].source;
        AudioSourceAccess::control(*oracle.audio, leaf, AudioSourceOperation::OneShot);
        unsigned quanta = 0;
        do {
            require(++quanta <= 8192, "Playlist event failed to complete");
            std::array<float, 512> a{}, b{}, expected{};
            authored.audio->read_offline(a);
            exported.audio->read_offline(b);
            oracle.audio->read_offline(expected);
            if (a != b || a != expected) {
                std::cerr << Json{{"event", event},
                                  {"quantum", quanta},
                                  {"source_export_equal", a == b},
                                  {"cue", AudioSourceAccess::status(*authored.audio,
                                                                    authored.scene.reference(cue))},
                                  {"ordinary", AudioSourceAccess::status(*oracle.audio, leaf)}}
                                 .dump()
                          << '\n';
                throw std::runtime_error(
                    "Playlist PCM differs from independently ordered ordinary Source requests");
            }
            for (float sample : b) {
                require(std::isfinite(sample), "Playlist PCM is nonfinite");
                energy += double(sample) * sample;
            }
        } while (AudioSourceAccess::status(*oracle.audio, leaf).at("one_shots") != 0);
        const auto status =
            AudioSourceAccess::status(*exported.audio, exported.scene.reference(cue));
        require(status.at("accepted_triggers") == event + 1 &&
                    status.at("last_sources") == Json::array({leaf}),
                "Playlist did not retain the requested low/middle/high order");
    }
    for (auto* game : {&authored, &exported}) {
        game->audio->poll_loading();
        const auto status = AudioSourceAccess::status(*game->audio, game->scene.reference(cue));
        require(status.at("repeat_finished") && status.at("repeat_issued_events") == 3 &&
                    game->audio->status().at("tick") == 0,
                "Saved/exported playlist did not finish once without game ticks");
    }
    require(energy > 1e-5 && authored.scene.snapshot() == authored_before &&
                exported.scene.snapshot() == exported_before,
            "Playlist playback was silent or mutated authored content");
    for (const auto* path : {playlist_scene_path, playlist_prefab_path})
        std::filesystem::copy_file(root / "source-hidden" / path,
                                   std::filesystem::absolute(scratch) /
                                       std::filesystem::path(path).filename(),
                                   std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove_all(root);
    std::cout << "Playlist Scene/portable Prefab, source-hidden relocated Windows-target content "
                 "and exact independent ordinary-Source PCM passed\n";
}

void publish_sound_playlist_gallery(const std::filesystem::path& proof,
                                    const std::filesystem::path& root) {
    require(!std::filesystem::exists(root / playlist_scene_path) &&
                !std::filesystem::exists(root / playlist_prefab_path),
            "Playlist Gallery already exists");
    ProjectLease lease(root);
    EngineContext engine;
    Scene scene(engine.world());
    PrefabLibrary prefabs(root);
    const auto prefab = read(proof / std::filesystem::path(playlist_prefab_path).filename());
    const auto asset = prefabs.create(scene, PrefabDocument(prefab), playlist_prefab_path);
    const auto document = read(proof / std::filesystem::path(playlist_scene_path).filename());
    require(document.at("asset_id") == playlist_scene_id,
            "Proved playlist Scene has wrong identity");
    prefabs.load_scene(scene, document);
    scene.save(root / playlist_scene_path);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add(prefabs.records().at(asset));
    (void)catalog.add_scene(playlist_scene_path);
    catalog.save(AssetCatalog::project_index(root));
}
