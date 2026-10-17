#pragma once
// The shared Running fixture uses actual animation, audio, PCM and Scene owners.
void audio_cue_gallery_run(const std::filesystem::path& root, const Json& source,
                           const std::filesystem::path& path) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto original_catalog = catalog->document(), original_source = read(root / path);
    Running run(root);
    run.load(source, catalog);
    run.ready();
    check(run.audio->status().at("one_shots") == 0 && run.signal(12000).energy == 0,
          "Gallery70 started sound before a forward sequence tick");
    check(run.animation->checkpoint().at("version") == 5 &&
              run.animation->checkpoint().at("audio_cues").size() == 3 &&
              run.animation->checkpoint().at("camera_shots").size() == 2,
          "Gallery70 lost exact combined cue/shot recovery");
    std::array<EntityRef, 3> emitters;
    EntityId moving, front, side;
    for (const auto& row : source.at("entities")) {
        const auto& values = row.at("components");
        const auto id = row.at("id").get<EntityId>();
        if (values.contains("forge.audio_cue")) {
            const auto& cue = values.at("forge.audio_cue");
            emitters.at(unsigned(cue.at("time_seconds").get<double>() / 2)) =
                cue.at("source").get<EntityRef>();
        }
        if (values.contains("forge.transform_animator"))
            moving = id;
        if (values.contains("forge.camera_shot")) {
            const auto& shot = values.at("forge.camera_shot");
            (shot.at("start_seconds") == 0 ? front : side) =
                shot.at("camera").get<EntityRef>().entity;
        }
    }
    check(bool(moving) && bool(front) && bool(side), "Gallery70 visual actors missing");
    unsigned heard = 0;
    double energy = 0;
    for (unsigned step = 1; step <= 52; ++step) {
        run.simulation.tick(.25f);
        const auto time = std::fmod(step * .25, 6.);
        const auto checkpoint = run.animation->checkpoint();
        const auto image = extract_render_scene(run.simulation.presentation(1));
        check(image.diagnostics.empty() && image.meshes.size() == 5 && image.lights.size() == 1 &&
                  image.cameras.size() == 1 &&
                  image.cameras.front().entity == (time < 3 ? front : side),
              "Gallery70 sound integration lost actual geometry or sequence camera cuts");
        const auto u = std::clamp((time - .5) / 2., 0., 1.);
        near(run.scene.entity(moving.str()).get<LocalTranslation>().x,
             -2 + 4 * u * u * (3 - 2 * u));
        check(run.animation->checkpoint() == checkpoint,
              "Gallery70 render extraction advanced cue/sequence time");
        const bool crossing = step == 1 || step % 8 == 0;
        if (crossing) {
            const unsigned index = step == 1 ? 0 : (step / 8) % 3;
            check(run.audio->status().at("one_shots") == 1 &&
                      AudioSourceAccess::status(*run.audio, emitters[index]).at("one_shots") == 1,
                  "Gallery70 emitted missing, wrong-source or duplicate loop-boundary sound");
            (void)run.signal(1024);
            const auto signal = run.signal(10976);
            const double frequency = index == 0 ? 330 : index == 1 ? 440 : 660;
            check(signal.energy > .00001 && std::abs(signal.frequency - frequency) < 15,
                  "Gallery70 cue did not render the expected real pitched tone");
            energy += signal.energy;
            ++heard;
        } else {
            const auto signal = run.signal(12000);
            if (step == 7 || step == 15 || step == 23)
                check(signal.energy == 0, "Gallery70 retained unwanted sound between cue points");
        }
    }
    check(heard == 7 && energy > .0001, "Gallery70 two-loop PCM cue count/energy differs");
    check(read(root / path) == original_source &&
              AssetCatalog::open_project(root).document() == original_catalog,
          "Gallery70 playback changed source/catalog intent");
    std::cout << "Gallery70 actual PCM cues=" << heard << ", energy=" << energy << '\n';
}
void audio_cue_gallery(const std::filesystem::path& original,
                       const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(original, root, std::filesystem::copy_options::recursive);
    const auto path = std::filesystem::path("Scenes/70-sequence-audio-cues.scene.json");
    const auto source = read(root / path);
    audio_cue_gallery_run(root, source, path);
    const auto game = root.string() + "-game", relocated = game + "-relocated";
    const std::array roots{source.at("asset_id").get<AssetId>()};
    EngineContext author;
    Scene schema(author.world());
    {
        ProjectLease lease(root);
        prepare_runtime_content_catalog(lease, roots, {}, schema.schema());
        package_runtime_content(root, game, roots, {"windows", "d3d12"}, {}, {}, schema.schema());
    }
    std::filesystem::rename(game, relocated);
    std::filesystem::rename(root, root.string() + "-source-offline");
    const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
    check(catalog.records().size() == 4, "Gallery70 export retained unrelated assets");
    check(!std::filesystem::exists(std::filesystem::path(relocated) / "Assets/FalloffTone.wav"),
          "Gallery70 export retained the source WAV");
    const auto scene = catalog.resolve(AssetRef<SceneAsset>{roots.front()});
    check(scene.state == AssetState::Available, "Relocated Gallery70 Scene is unavailable");
    audio_cue_gallery_run(relocated, read(std::filesystem::path(relocated) / scene.record->source),
                          scene.record->source);
}
