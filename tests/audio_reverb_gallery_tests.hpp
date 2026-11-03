#pragma once
constexpr auto reverb_gallery_path = "Scenes/148-source-reverb.scene.json";
void author_reverb_gallery(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / reverb_gallery_path), "Reverb Gallery already exists");
    ProjectLease lease(root);
    EngineContext author;
    Scene scene(author.world());
    auto definition = duplicate_scene_asset(read(root / "Scenes/70-sequence-audio-cues.scene.json"),
                                            AssetId::generate(), scene.schema());
    for (auto& row : definition["entities"]) {
        auto& values = row["components"];
        if (values.contains("forge.animation_sequence")) {
            values["forge.animation_sequence"]["duration"] = 10;
            row["name"] = "Ten-second sequence - dry, bright and damped reverb";
        }
        if (values.contains("forge.audio_source")) {
            const auto name = row.at("name").get<std::string>();
            const bool dry = name.starts_with("Red"), bright = name.starts_with("Green");
            auto& sound = values["forge.audio_source"];
            sound["pitch"] = 1;
            sound["bus"] = 2;
            sound["reverb_enabled"] = !dry;
            sound["reverb_decay"] = .6;
            sound["reverb_damping"] = bright ? .1 : .8;
            sound["reverb_wet"] = .5;
            row["name"] = dry      ? "Red - dry at 0 seconds"
                          : bright ? "Green - bright reverb at 3 seconds"
                                   : "Blue - damped reverb at 6 seconds";
        }
        if (values.contains("forge.audio_cue")) {
            auto& cue = values["forge.audio_cue"];
            const auto time = cue.at("time_seconds").get<double>() * 1.5;
            cue["time_seconds"] = time;
            row["name"] = time == 0   ? "Dry cue - 0s"
                          : time == 3 ? "Bright reverb cue - 3s"
                                      : "Damped reverb cue - 6s";
        }
    }
    scene.reset(definition);
    scene.save(root / reverb_gallery_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(reverb_gallery_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
}
std::vector<float> render_reverb_gallery(const std::filesystem::path& root, const Json& source) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    Running run(root);
    run.load(source, catalog);
    run.ready();
    check(run.audio->status().at("one_shots") == 0 && run.signal(12000).energy == 0,
          "Reverb Gallery played before its first sequence tick");
    std::vector<float> result;
    result.reserve(660 * 1600);
    unsigned peak_shots = 0;
    for (unsigned frame = 0; frame < 660; ++frame) {
        run.simulation.tick(1.f / 60);
        const auto offset = result.size();
        result.resize(offset + 1600);
        run.audio->read_offline(std::span(result).subspan(offset));
        peak_shots = std::max(peak_shots, run.audio->status().at("one_shots").get<unsigned>());
    }
    check(peak_shots == 1 && run.audio->status().at("failed_sources") == 0,
          "Reverb Gallery lost its separated cue/voice lifecycle");
    for (float sample : result)
        check(std::isfinite(sample), "Reverb Gallery produced invalid PCM");
    return result;
}
void reverb_gallery(const std::filesystem::path& original, const std::filesystem::path& scratch) {
    const auto stage = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto root = stage / "source";
    std::filesystem::create_directories(stage);
    std::filesystem::copy(original, root, std::filesystem::copy_options::recursive);
    const auto source = read(root / reverb_gallery_path);
    const auto authored = render_reverb_gallery(root, source);
    auto dry_source = source;
    for (auto& row : dry_source["entities"])
        if (row["components"].contains("forge.audio_source"))
            row["components"]["forge.audio_source"]["reverb_enabled"] = false;
    const auto dry = render_reverb_gallery(root, dry_source);
    auto energy = [](const auto& samples, double begin, double end) {
        double result = 0;
        const auto first = std::size_t(begin * 48000) * 2, last = std::size_t(end * 48000) * 2;
        for (auto i = first; i < last; ++i)
            result += double(samples[i]) * samples[i];
        return result / (last - first);
    };
    check(energy(authored, .2, .8) > 1e-8 && energy(authored, 3.2, 3.8) > 1e-8 &&
              energy(authored, 6.2, 6.8) > 1e-8,
          "Dry/bright/damped Gallery cues were not audible in offline PCM");
    check(energy(authored, 1.2, 2.7) == 0 && energy(authored, 9.1, 9.7) == 0,
          "Dry interval or bounded reverb retirement is not silent");
    check(energy(dry, 4.15, 4.45) == 0 && energy(dry, 7.15, 7.45) == 0 &&
              energy(authored, 4.15, 4.45) > 1e-12 && energy(authored, 7.15, 7.45) > 1e-12,
          "Gallery reverb tails were absent or came from the original clip instead of DSP");
    const std::array roots{source.at("asset_id").get<AssetId>()};
    EngineContext author;
    Scene schema(author.world());
    {
        ProjectLease lease(root);
        prepare_runtime_content_catalog(lease, roots, {}, schema.schema());
        package_runtime_content(root, stage / "game", roots, {"windows", "d3d12"}, {}, {},
                                schema.schema());
    }
    std::filesystem::rename(stage / "game", stage / "relocated");
    std::filesystem::rename(root, stage / "sources-offline");
    const auto catalog = open_runtime_content(stage / "relocated", {"windows", "d3d12"});
    const auto record = catalog.resolve(AssetRef<SceneAsset>{roots.front()});
    check(record.state == AssetState::Available &&
              !std::filesystem::exists(stage / "relocated/Assets/FalloffTone.wav"),
          "Reverb export retained source audio or lost its Scene");
    const auto exported = render_reverb_gallery(stage / "relocated",
                                                read(stage / "relocated" / record.record->source));
    check(authored.size() == exported.size() &&
              std::memcmp(authored.data(), exported.data(), authored.size() * sizeof(float)) == 0,
          "Source-hidden relocated reverb PCM changed");
    check(read(stage / "sources-offline" / reverb_gallery_path) == source,
          "Reverb playback changed authored Scene intent");
    std::ofstream stream(stage / "reverb-stereo-f32.bin", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(authored.data()), authored.size() * sizeof(float));
    check(bool(stream), "Reverb PCM evidence write failed");
    std::cout << "Gallery148: three cues, actual bounded reverb tails and exact source-hidden PCM: "
              << stage << '\n';
}
