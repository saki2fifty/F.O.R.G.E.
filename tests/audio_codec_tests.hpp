// Original analytical audio sources; both direct and supervised recipe consumers.
void codec_tests(const std::filesystem::path& root, bool direct,
                 const std::filesystem::path& worker) {
    std::filesystem::create_directories(root / "Assets");
    ProjectLease lease(root);
    AssetPublisher publisher(lease);
    std::vector<AssetId> roots;
    std::vector<AudioClipData> expected_clips;
    std::vector<std::filesystem::path> original_sources;
    for (const auto& profile : audio_source_profiles) {
        if (profile.format == AudioSourceFormat::Wav)
            continue; // Existing WAV tests cover this compatibility path.
        auto importer = audio_importer(worker, profile);
        for (const auto* name : {"Mono", "Stereo"}) {
            const auto fixtures = std::filesystem::path(FORGE_AUDIO_CODEC_FIXTURES);
            const auto source = read_bytes(fixtures / (name + std::string(profile.extension)),
                                           max_audio_source_bytes);
            const auto reference = decode_audio_wav(read_bytes(
                fixtures / (std::string(name) + "Reference.wav"), max_audio_source_bytes));
            const auto decoded = decode_audio_source(source, profile.format);
            check(decoded.channels == reference.channels && decoded.rate == reference.rate &&
                      decoded.frames == reference.frames,
                  "Codec changed frame count, channels or sample rate");
            double difference = 0, energy = 0;
            for (std::size_t i = 0; i < reference.pcm.size(); ++i) {
                const auto error = double(decoded.pcm[i]) - reference.pcm[i];
                difference += error * error;
                energy += double(reference.pcm[i]) * reference.pcm[i];
            }
            check(profile.format == AudioSourceFormat::Flac ? decoded.pcm == reference.pcm
                                                            : difference / energy < .01,
                  "Codec differs from original independent PCM reference");
            rejects([&] { decode_audio_wav(source); });
            rejects([&] { decode_audio_source(source, static_cast<AudioSourceFormat>(99)); });
            rejects([&] { decode_audio_source(std::span(source).first(12), profile.format); });
            rejects([&] { decode_audio_source(wav(), profile.format); });
            rejects([&] {
                decode_audio_source(std::span(source).first(source.size() / 2), profile.format);
            });
            if (profile.format == AudioSourceFormat::Flac) {
                // FLAC STREAMINFO packs rate/channels/bits/total frames into
                // eight big-endian bytes. Reject bounds before allocating PCM.
                auto altered = source;
                std::uint64_t info = 0;
                for (unsigned i = 0; i < 8; ++i)
                    info = (info << 8) | std::to_integer<unsigned>(altered[18 + i]);
                auto change_info = [&](std::uint64_t value) {
                    for (unsigned i = 0; i < 8; ++i)
                        altered[18 + i] = std::byte(value >> ((7 - i) * 8));
                };
                change_info((info & ~(std::uint64_t(7) << 41)) | (std::uint64_t(2) << 41));
                rejects([&] { decode_audio_source(altered, profile.format); });
                change_info((info & ~((std::uint64_t(1) << 36) - 1)) |
                            ((std::uint64_t(1) << 36) - 1));
                rejects([&] { decode_audio_source(altered, profile.format); });
            }
            const auto locator = "Assets/" + std::string(name) + std::string(profile.extension);
            write(root / locator, source);
            AssetImportRequest request{AssetId::generate(),
                                       root,
                                       locator,
                                       {"windows", "d3d12", "desktop"},
                                       {std::string(profile.importer)}};
            const auto plan = importer->discover(request, {});
            auto input =
                ImportProcessRequest{{{"recipe", profile.importer},
                                      {"revision", audio_recipe_revision()},
                                      {"source_digest", content_digest(source)},
                                      {"settings", request.settings}},
                                     {{"source" + std::string(profile.extension), source}}};
            auto cook = [&] {
                return direct ? execute_audio_recipe(input)
                              : importer->import_and_cook(request, plan, {}, {});
            };
            const auto files = cook();
            check(validate_audio_bundle(files).at("source_format").get<std::string>() ==
                      profile.extension.substr(1),
                  "Codec metadata reports a different source format");
            check(audio_bundle_clip(files).pcm == decoded.pcm &&
                      cook().front().bytes == files.front().bytes,
                  "Codec cook changed samples or was nondeterministic");
            auto invalid = input;
            invalid.inputs.front().name = "source.wav";
            rejects([&] { execute_audio_recipe(invalid); });
            invalid = input;
            invalid.payload["settings"]["importer"] = "forge.audio.wav";
            rejects([&] { execute_audio_recipe(invalid); });
            std::stop_source cancelled;
            cancelled.request_stop();
            rejects([&] { execute_audio_recipe(input, cancelled.get_token()); });
            auto mono_request = input;
            mono_request.payload["settings"] =
                ImportSettingsDocument{std::string(profile.importer), 1, {{"force_mono", true}}};
            const auto mono = audio_bundle_clip(execute_audio_recipe(mono_request));
            check(mono.channels == 1 && mono.frames == decoded.frames,
                  "Codec import did not share processing settings");
            for (std::size_t frame = 0; frame < mono.frames; ++frame) {
                const auto expected =
                    decoded.channels == 1
                        ? decoded.pcm[frame]
                        : float((double(decoded.pcm[frame * 2]) + decoded.pcm[frame * 2 + 1]) * .5);
                check(mono.pcm[frame] == expected, "Codec downmix differs");
            }
            AssetPublicationCandidate candidate;
            candidate.ticket = publisher.capture(request.asset, request.source);
            candidate.input = plan.input;
            candidate.sidecar.settings = request.settings;
            candidate.sidecar.build_inputs = plan.input.document();
            candidate.files = files;
            prepare_audio_publication(candidate, plan);
            const auto published =
                publisher.publish(std::move(candidate), *importer, [](const auto&, const auto&) {});
            const auto record = published.catalog.records().at(request.asset);
            roots.push_back(record.id);
            expected_clips.push_back(decoded);
            original_sources.push_back(request.source);
            check(load_audio_clip(root, record).pcm == decoded.pcm,
                  "Runtime selected other codec PCM");
            auto wrong_format = record;
            wrong_format.metadata["forge.import"]["importer"] = "forge.audio.wav";
            rejects([&] { load_audio_clip(root, wrong_format); });
            // Selected PCM survives malformed or absent source audio.
            write(root / locator, std::vector<std::byte>(20, std::byte{0}));
            const auto broken = importer->discover(request, {});
            rejects([&] {
                if (!direct)
                    importer->import_and_cook(request, broken, {}, {});
                else {
                    auto malformed = input;
                    malformed.inputs.front().bytes.assign(20, std::byte{0});
                    malformed.payload["source_digest"] =
                        content_digest(malformed.inputs.front().bytes);
                    execute_audio_recipe(malformed);
                }
            });
            check(load_audio_clip(root, record).pcm == decoded.pcm,
                  "Failed codec reimport lost last-good data");
            std::filesystem::remove(root / locator);
            EngineContext engine(WorldRole::Runtime, false,
                                 {audio_module({root, AudioOutput::Offline, true})});
            Scene scene(engine.world());
            scene.replace({{"version", 1},
                           {"entities", Json::array({{{"id", "speaker"},
                                                      {"name", "Codec speaker"},
                                                      {"components",
                                                       {{"forge.audio_source",
                                                         {{"clip", request.asset},
                                                          {"spatialized", false},
                                                          {"play_on_start", true},
                                                          {"loop", true},
                                                          {"gain", 1},
                                                          {"pitch", 1},
                                                          {"minimum_distance", 1},
                                                          {"maximum_distance", 100}}}}}}})}});
            RuntimeSimulation simulation(engine.world(), scene);
            simulation.sync_audio();
            auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
            check(audio->wait_for_sources(std::chrono::seconds(5)),
                  "Codec runtime preparation failed");
            audio->paused(false);
            std::vector<float> output(1024);
            audio->read_offline(output);
            check(std::any_of(output.begin(), output.end(),
                              [](float x) { return std::abs(x) > .01f; }),
                  "Codec cooked data produced no runtime audio");
            AudioClipPreview preview;
            preview.start(root, record, AudioOutput::Offline);
            await_audio_preview(preview);
            preview.load_waveform(root, record);
            await_audio_preview(preview);
            check(preview.waveform() && preview.waveform()->channels == decoded.channels,
                  "Codec editor preview did not retain channel identity");
            preview.seek(.05);
            preview.read_offline(output);
            check(std::any_of(output.begin(), output.end(),
                              [](float x) { return std::abs(x) > .01f; }),
                  "Codec editor preview depended on source audio");
            preview.clear();
        }
    }
    EngineContext engine(WorldRole::Runtime, false,
                         {audio_module({root, AudioOutput::Offline, true})});
    Scene scene(engine.world());
    const auto exported = root.parent_path() / "codec-export";
    prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
    package_runtime_content(root, exported, roots, {"windows", "d3d12"}, {}, {}, scene.schema());
    const auto relocated = root.parent_path() / "codec-export-relocated";
    std::filesystem::rename(exported, relocated);
    const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
    for (std::size_t i = 0; i < roots.size(); ++i) {
        const auto& record = catalog.records().at(roots[i]);
        check(!std::filesystem::exists(relocated / original_sources[i]),
              "Codec source leaked into exported runtime content");
        check(load_audio_clip(relocated, record).pcm == expected_clips[i].pcm,
              "Relocated content lost selected codec samples");
    }
    std::filesystem::remove_all(relocated);
}
