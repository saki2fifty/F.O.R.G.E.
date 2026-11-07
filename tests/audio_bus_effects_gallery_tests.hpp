#pragma once
#include <algorithm>
// Uses the existing audio Gallery's Game, read, schema and package owners.
constexpr auto bus_effects_scene = "Scenes/196-shared-audio-bus-effects.scene.json";
constexpr auto bus_effects_scene_id = "f1c639b9-af96-4587-bf43-61eb62cf089a";
AudioRouting bus_effects_routes() {
    AudioBusEffects echo, room;
    echo.echo_enabled = true;
    echo.echo_seconds = .09f;
    echo.echo_feedback = .5f;
    room.low_pass_enabled = true;
    room.low_pass_hz = 4200;
    room.reverb_enabled = true;
    room.reverb_decay = .4f;
    room.reverb_wet = .3f;
    return {{{"gallery_echo", "Shared echo", .6f, "gallery_room", {{"master", .25f}}, echo},
             {"gallery_room", "Shared room", .7f, "effects", {}, room}}};
}
void author_bus_effects_gallery(const std::filesystem::path& root,
                                const std::filesystem::path& proved_scene = {}) {
    require(!std::filesystem::exists(root / bus_effects_scene),
            "Bus effects Gallery already exists");
    ProjectLease lease(root);
    ProjectSettings project(root);
    auto settings = project.document();
    auto routing = audio_routing_settings(settings.at("game").at("audio"));
    for (auto& bus : bus_effects_routes().buses) {
        require(std::ranges::none_of(routing.buses,
                                     [&](const auto& existing) { return existing.key == bus.key; }),
                "Bus effects example key already exists");
        routing.buses.push_back(std::move(bus));
    }
    settings["game"]["audio"]["routing"] = audio_routing_json(routing);
    EngineContext engine;
    Scene scene(engine.world());
    auto document =
        proved_scene.empty()
            ? duplicate_scene_asset(read(root / "Scenes/33-audio-mixer.scene.json"),
                                    AssetId::parse(bus_effects_scene_id), scene.schema())
            : read(proved_scene);
    require(document.at("asset_id") == bus_effects_scene_id, "Wrong proved bus example identity");
    if (proved_scene.empty())
        for (auto& row : document["entities"]) {
            auto& values = row["components"];
            if (!values.contains("forge.audio_source"))
                continue;
            auto& sound = values["forge.audio_source"];
            const auto stock = sound.at("bus").get<unsigned>();
            sound["custom_bus"] = stock == 3 ? "gallery_room" : "gallery_echo";
            sound["pause_with_game"] = stock != 1;
            row["name"] = stock == 1   ? "Independent tone - shared echo and room"
                          : stock == 2 ? "Pausable tone - shared echo and room"
                                       : "Pausable tone - shared room";
        }
    scene.reset(document);
    auto catalog = AssetCatalog::open_project(root);
    require(!catalog.records().contains(AssetId::parse(bus_effects_scene_id)),
            "Bus effects Scene identity is already registered");
    if (proved_scene.empty())
        scene.save(root / bus_effects_scene);
    else
        std::filesystem::copy_file(proved_scene, root / bus_effects_scene);
    (void)catalog.add_scene(bus_effects_scene);
    catalog.save(AssetCatalog::project_index(root));
    project.save(std::move(settings));
}
Json bus_effects_oracle(const Json& document) {
    auto oracle = document;
    oracle["entities"] = Json::array();
    const auto fx = bus_effects_routes();
    auto combined = fx.buses[1].effects;
    combined.echo_enabled = true;
    combined.echo_seconds = .09f;
    combined.echo_feedback = .5f;
    unsigned sounds = 0;
    for (auto row : document.at("entities")) {
        auto& values = row["components"];
        if (!values.contains("forge.audio_source")) {
            oracle["entities"].push_back(std::move(row));
            continue;
        }
        ++sounds;
        auto& source = values["forge.audio_source"];
        const auto key = source.at("custom_bus").get<std::string>();
        require(key == "gallery_echo" || key == "gallery_room", "Unexpected Gallery source route");
        const auto gain = source.at("gain").get<float>();
        source["custom_bus"] = "";
        source["bus"] = 0;
        source.update(
            audio_bus_effects_json(key == "gallery_echo" ? combined : fx.buses[1].effects));
        source["gain"] = gain * (key == "gallery_echo" ? .6f * .7f : .7f);
        if (key == "gallery_echo") {
            auto direct = row;
            direct["id"] = EntityId::generate().str();
            auto& sound = direct["components"]["forge.audio_source"];
            sound.update(audio_bus_effects_json(fx.buses[0].effects));
            sound["gain"] = gain * .6f * .25f;
            oracle["entities"].push_back(std::move(direct));
        }
        oracle["entities"].push_back(std::move(row));
    }
    require(sounds == 3, "Shared effects Gallery needs three original pitched tones");
    return oracle;
}
void verify_bus_effects_gallery(const std::filesystem::path& gallery,
                                const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto source = root / "source", package = root / "package", moved = root / "relocated";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
    if (!std::filesystem::exists(source / bus_effects_scene))
        author_bus_effects_gallery(source);
    const auto document = read(source / bus_effects_scene);
    const auto settings = ProjectSettings(source).document();
    const auto routing = audio_routing_settings(settings.at("game").at("audio"));
    for (const auto& expected : bus_effects_routes().buses)
        require(std::ranges::find(routing.buses, expected) != routing.buses.end(),
                "Shared effects Gallery settings differ from the independent oracle");
    EngineContext inspector;
    Scene schema(inspector.world());
    const std::array roots{AssetId::parse(bus_effects_scene_id)};
    {
        ProjectLease lease(source);
        prepare_runtime_content_catalog(lease, roots, {}, schema.schema());
    }
    const RuntimePackageTarget target{"windows", "d3d12"};
    package_runtime_content(source, package, roots, target);
    std::filesystem::rename(package, moved);
    const auto catalog = open_runtime_content(moved, target);
    const auto cooked = read(moved / catalog.records().at(roots[0]).source);
    for (const auto& file : std::filesystem::recursive_directory_iterator(moved))
        require(file.path().extension() != ".wav", "Bus Gallery retained an authoring recording");
    Json kit{{"runtime_audio_routing", 1}};
    bool rejected = false;
    try {
        validate_runtime_compatibility(catalog, kit, &settings);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Actual processed Gallery admitted a routing-only runtime kit");
    kit["runtime_audio_bus_effects"] = audio_bus_effects_profile_version;
    validate_runtime_compatibility(catalog, kit, &settings);
    Game authored(source, document, routing), oracle(source, bus_effects_oracle(document), {});
    std::filesystem::rename(source, root / "source-hidden");
    Game exported(moved, cooked, routing);
    const auto authored_before = authored.scene.snapshot(),
               exported_before = exported.scene.snapshot();
    double energy = 0;
    const auto compare = [&](unsigned blocks, bool independent = true) {
        for (unsigned block = 0; block < blocks; ++block) {
            std::array<float, 512> a{}, b{}, expected{};
            authored.audio->read_offline(a);
            exported.audio->read_offline(b);
            oracle.audio->read_offline(expected);
            require(a == b, "Moved source-hidden shared-bus PCM differs from authored output");
            for (unsigned i = 0; i < a.size(); ++i) {
                require(std::isfinite(a[i]) &&
                            (!independent || std::abs(a[i] - expected[i]) < 2e-5f),
                        "Shared-bus Gallery differs from independent ordinary-source path PCM");
                energy += double(a[i]) * a[i];
            }
        }
    };
    compare(256);
    for (auto* game : {&authored, &exported, &oracle})
        game->audio->game_paused(true);
    compare(128);
    for (auto* game : {&authored, &exported, &oracle})
        game->audio->game_paused(false);
    compare(128);
    require(energy > .01, "Shared effects Gallery PCM is silent");
    auto volumes = exported.audio->status().at("bus_volumes").get<AudioBusVolumes>();
    volumes.at("gallery_echo") = volumes.at("gallery_room") = 0;
    for (auto* game : {&authored, &exported})
        game->audio->mix_volumes(1, {1, 1, 1}, volumes);
    compare(8, false);
    std::array<float, 2048> muted{};
    exported.audio->read_offline(muted);
    require(std::ranges::all_of(muted, [](float sample) { return std::abs(sample) < 1e-12f; }),
            "Muted shared effects leaked their retained tails");
    require(authored.scene.snapshot() == authored_before &&
                exported.scene.snapshot() == exported_before &&
                exported.audio->status().at("tick") == 0 &&
                exported.audio->status().at("voices") == 3,
            "Shared bus controls changed authored state, fixed time or source voice ownership");
    std::filesystem::create_directories(scratch / "proved");
    std::filesystem::copy_file(root / "source-hidden" / bus_effects_scene,
                               scratch / "proved" / "196-shared-audio-bus-effects.scene.json",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove_all(root);
    std::cout << "Shared bus Gallery: independent path PCM, both pause domains, faders and "
                 "source-hidden relocated cooked content passed\n";
}
