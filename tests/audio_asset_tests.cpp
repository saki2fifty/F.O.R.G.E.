#include "asset_bytes.hpp"
#include "asset_file_operations.hpp"
#include "asset_storage.hpp"
#include "audio_authoring.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
#include "audio_selection.hpp"
#include "runtime_package.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <forge/audio.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <source_location>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F&& f, std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Audio fixture was accepted at " + std::to_string(where.line()));
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    check(bool(out.write(reinterpret_cast<const char*>(bytes.data()),
                         std::streamsize(bytes.size()))) &&
              bool(out.flush()),
          "Audio fixture write failed");
}
std::vector<std::byte> wav(unsigned channels = 1, unsigned rate = 48000) {
    std::vector<std::byte> bytes;
    auto put = [&](unsigned value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            bytes.push_back(std::byte(value >> (i * 8)));
    };
    auto text = [&](std::string_view value) {
        for (auto c : value)
            bytes.push_back(std::byte(c));
    };
    constexpr unsigned frames = 480;
    text("RIFF");
    put(36 + frames * channels * 2, 4);
    text("WAVEfmt ");
    put(16, 4);
    put(1, 2);
    put(channels, 2);
    put(rate, 4);
    put(rate * channels * 2, 4);
    put(channels * 2, 2);
    put(16, 2);
    text("data");
    put(frames * channels * 2, 4);
    for (unsigned i = 0; i < frames * channels; ++i)
        put(i % 2 ? 4096 : 2048, 2);
    return bytes;
}
void format_tests() {
    for (unsigned channels : {1u, 2u})
        for (unsigned rate : {24000u, 48000u, 96000u}) {
            const auto source = wav(channels, rate);
            const auto clip = decode_audio_wav(source);
            check(clip.frames == 480 && clip.channels == channels && clip.rate == rate &&
                      clip.pcm[0] == .0625f && clip.pcm[1] == .125f,
                  "Native WAV decode differs");
            const auto cooked = encode_audio_clip(clip);
            const auto decoded = decode_audio_clip(cooked);
            check(decoded.pcm == clip.pcm && decoded.channels == channels && decoded.rate == rate,
                  "Cooked AudioClip roundtrip failed");
            for (std::size_t n :
                 {std::size_t(0), std::size_t(7), std::size_t(31), cooked.size() - 1})
                rejects([&] { decode_audio_clip(std::span(cooked).first(n)); });
            for (unsigned at : {0u, 8u, 12u, 16u, 20u, 31u}) {
                auto bad = cooked;
                bad[at] ^= std::byte{255};
                if (at == 16) {
                    bad[16] = bad[17] = bad[18] = bad[19] = std::byte{255};
                }
                rejects([&] { decode_audio_clip(bad); });
            }
            auto bad = cooked;
            bad[32] = bad[33] = std::byte{0};
            bad[34] = std::byte{0x80};
            bad[35] = std::byte{0x7f};
            rejects([&] { decode_audio_clip(bad); });
            bad = cooked;
            bad.push_back(std::byte{});
            rejects([&] { decode_audio_clip(bad); });
        }
    rejects([&] { decode_audio_wav(wav(3)); });
    rejects([&] { decode_audio_wav(wav(1, 0)); });
    rejects([&] { decode_audio_wav(wav(1, max_audio_sample_rate + 1)); });
    auto bad = wav();
    bad.resize(20);
    rejects([&] { decode_audio_wav(bad); });
    bad.assign(max_audio_source_bytes + 1, std::byte{});
    rejects([&] { decode_audio_wav(bad); });
}
std::vector<std::byte> float_wav(const AudioClipData& clip) {
    std::vector<std::byte> bytes;
    auto put = [&](std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            bytes.push_back(std::byte(value >> (i * 8)));
    };
    auto text = [&](std::string_view word) {
        for (char c : word)
            bytes.push_back(std::byte(c));
    };
    text("RIFF");
    put(36 + clip.pcm.size() * 4, 4);
    text("WAVEfmt ");
    put(16, 4);
    put(3, 2);
    put(clip.channels, 2);
    put(clip.rate, 4);
    put(clip.rate * clip.channels * 4, 4);
    put(clip.channels * 4, 2);
    put(32, 2);
    text("data");
    put(clip.pcm.size() * 4, 4);
    for (float sample : clip.pcm)
        put(std::bit_cast<std::uint32_t>(sample), 4);
    return bytes;
}
void processing_tests(const std::filesystem::path& root, bool direct,
                      const std::filesystem::path& worker) {
    auto cook = [&](const AudioClipData& clip, const Json& overrides) {
        const auto source = float_wav(clip);
        ImportProcessRequest request{
            {{"recipe", "forge.audio.wav"},
             {"revision", audio_recipe_revision()},
             {"source_digest", content_digest(source)},
             {"settings", ImportSettingsDocument{"forge.audio.wav", 1, overrides}}},
            {{"source.wav", source}}};
        auto output = direct ? execute_audio_recipe(request)
                             : run_import_process(worker, root, request, audio_worker_limits());
        return audio_bundle_clip(output);
    };
    const AudioClipData stereo{{.5f, .25f, -.25f, .5f, 0, 0}, 3, 2, 48000};
    check(cook(stereo, Json::object()).pcm == stereo.pcm,
          "Default audio processing changed source samples");
    const auto mono = cook(stereo, {{"force_mono", true}});
    check(mono.channels == 1 && mono.frames == 3 && mono.rate == 48000 &&
              mono.pcm == std::vector<float>{.375f, .125f, 0},
          "Stereo averaging differs");
    const auto normalized = cook(stereo, {{"normalize_peak", true}});
    check(normalized.channels == 2 && normalized.pcm == std::vector<float>{1, .5f, -.5f, 1, 0, 0},
          "Stereo peak normalization changed channel ratio");
    const auto both = cook(stereo, {{"force_mono", true}, {"normalize_peak", true}});
    check(both.pcm[0] == 1 && std::abs(both.pcm[1] - 1.f / 3) < 1e-6 && both.pcm[2] == 0,
          "Mono/normalization order differs");
    AudioClipData opposite{{.5f, -.5f, -.5f, .5f}, 2, 2, 24000};
    check(cook(opposite, {{"force_mono", true}, {"normalize_peak", true}}).pcm ==
              std::vector<float>{0, 0},
          "Antiphase mono normalization invented sound");
    AudioClipData extreme{
        {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()}, 1, 2, 48000};
    check(cook(extreme, {{"force_mono", true}, {"normalize_peak", true}}).pcm ==
              std::vector<float>{1},
          "Finite float downmix overflowed");
    AudioClipData tiny{{std::numeric_limits<float>::denorm_min(), 0}, 2, 1, 48000};
    check(cook(tiny, {{"normalize_peak", true}}).pcm == std::vector<float>{1, 0},
          "Tiny finite peak normalization overflowed or lost sound");
    for (unsigned rate : {24000u, 48000u, 96000u}) {
        AudioClipData padded{std::vector<float>(rate / 10 * 2, 0), rate / 10, 2, rate};
        for (unsigned frame = rate / 50; frame < rate * 8 / 100; ++frame)
            padded.pcm[frame * 2 + 1] = .5f;
        const auto trimmed = cook(padded, {{"trim_silence", true}});
        const auto fade = rate / 200;
        check(trimmed.frames == rate * 6 / 100 && trimmed.channels == 2 && trimmed.rate == rate &&
                  trimmed.pcm[1] == 0 && trimmed.pcm.back() == 0 &&
                  trimmed.pcm[(fade - 1) * 2 + 1] == .5f &&
                  trimmed.pcm[(trimmed.frames - fade) * 2 + 1] == .5f,
              "Trim duration/channel boundaries or rate-based fade differs");
        for (std::size_t i = 0; i < trimmed.pcm.size(); i += 2)
            check(trimmed.pcm[i] == 0, "Trimming mixed channels");
        check(std::abs(trimmed.pcm[3] - .5f / (fade - 1)) < 1e-7, "Trim edge ramp is not linear");
        const auto repeated = cook(padded, {{"trim_silence", true}});
        check(repeated.pcm == trimmed.pcm, "Processed cook is not deterministic");
        const auto normalized_trim =
            cook(padded, {{"normalize_peak", true}, {"trim_silence", true}});
        check(normalized_trim.frames == trimmed.frames &&
                  normalized_trim.pcm[(fade - 1) * 2 + 1] == 1,
              "Normalize did not precede trimming");
    }
    const auto short_clip = cook({{0, .25f, .5f, 0}, 4, 1, 48000}, {{"trim_silence", true}});
    check(short_clip.frames == 2 && short_clip.pcm == std::vector<float>{.25f, .5f},
          "Trim fades erased a very short clip");
    const auto quiet =
        cook({{0, 0, 0, 0}, 2, 2, 48000}, {{"trim_silence", true}, {"normalize_peak", true}});
    check(quiet.frames == 1 && quiet.pcm == std::vector<float>{0, 0},
          "Entirely quiet processing returned an empty/invalid clip");
    const auto threshold = cook({{.01f, .1f, .01f}, 3, 1, 48000},
                                {{"trim_silence", true}, {"trim_threshold_db", -30.}});
    check(threshold.frames == 1 && threshold.pcm == std::vector<float>{.1f},
          "Trim threshold did not control retained frames");
    const auto schema = audio_settings();
    for (const Json& invalid : {Json(-100.), Json(-1.), Json("-60"), Json(nullptr)})
        rejects([&] { (void)schema.edit({"forge.audio.wav"}, "trim_threshold_db", invalid); });
    rejects([&] { (void)schema.edit({"forge.audio.wav"}, "force_mono", 1); });
    ImportSettingsDocument invalid{"forge.audio.wav"};
    invalid.overrides["trim_threshold_db"] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)schema.effective(invalid); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Use --direct/--worker worker project");
        const bool direct = std::string_view(argv[1]) == "--direct";
        format_tests();
        const auto root = std::filesystem::absolute(argv[3]);
        if (std::filesystem::exists(root))
            std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "Assets");
        processing_tests(root, direct, std::filesystem::absolute(argv[2]));
        auto bytes = wav();
        write(root / "Assets/tone.wav", bytes);
        // Explicitly upgrade an existing registered record without changing ID.
        const auto original = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        auto importer =
            audio_importer(direct ? std::filesystem::path{} : std::filesystem::absolute(argv[2]));
        AssetImportRequest request{original.id,
                                   root,
                                   "Assets/tone.wav",
                                   {"windows", "d3d12", "desktop"},
                                   {"forge.audio.wav"}};
        auto plan = importer->discover(request, {});
        auto cook = [&](const AssetImportPlan& intended) {
            if (!direct)
                return importer->import_and_cook(request, intended, {}, {});
            return execute_audio_recipe(
                {{{"recipe", "forge.audio.wav"},
                  {"revision", audio_recipe_revision()},
                  {"source_digest", intended.input.source_digest},
                  {"settings", request.settings}},
                 {{"source.wav", read_bytes(root / request.source, max_audio_source_bytes)}}});
        };
        auto files = cook(plan);
        auto metadata = validate_audio_bundle(files);
        check(metadata.at("channels") == 1 && metadata.at("sample_rate") == 48000 &&
                  metadata.at("duration") == .01,
              "AudioClip preview metadata absent/wrong");
        check(cook(plan)[0].bytes == files[0].bytes, "Audio cook is not deterministic");
        auto corrupt = files;
        corrupt.push_back(files.front());
        rejects([&] { validate_audio_bundle(corrupt); });
        corrupt = files;
        corrupt.back().bytes = {std::byte{'{'}};
        rejects([&] { validate_audio_bundle(corrupt); });
        ProjectLease lease(root);
        AssetPublisher publisher(lease);
        auto candidate = [&](const auto& intended, auto output) {
            AssetPublicationCandidate c;
            c.ticket = publisher.capture(request.asset, request.source);
            c.input = intended.input;
            c.sidecar.settings = request.settings;
            c.sidecar.build_inputs = intended.input.document();
            c.files = std::move(output);
            prepare_audio_publication(c, intended);
            return c;
        };
        auto published =
            publisher.publish(candidate(plan, files), *importer, [](const auto&, const auto&) {});
        auto record = published.catalog.records().at(original.id);
        check(record.id == original.id && record.metadata.at("forge.audio") == metadata,
              "Audio import lost legacy identity or metadata");
        const auto loaded = load_audio_clip(root, record);
        check(loaded.pcm == decode_audio_wav(bytes).pcm, "Runtime selected other audio bytes");
        auto cache = DerivedDataCache(root).find(
            plan.input, [&](const auto& value) { importer->validate(value); });
        check(cache && cache->key == published.artifact.key, "Audio DDC hit absent");
        const auto good_index = read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        write(root / request.source, std::vector<std::byte>(20, std::byte{0}));
        const auto bad_plan = importer->discover(request, {});
        rejects([&] { cook(bad_plan); });
        check(read_bytes(root / "forge.assets.json", max_asset_index_bytes) == good_index &&
                  load_audio_clip(root, record).pcm == loaded.pcm,
              "Failed source replacement damaged last-good selected audio");
        // Cooked runtime loading does not depend on a development source being present.
        std::filesystem::remove(root / request.source);
        check(load_audio_clip(root, record).pcm == loaded.pcm, "Cooked audio requires WAV source");
        auto wrong = record;
        wrong.metadata["forge.import"]["artifact_digest"] = std::string(64, '0');
        rejects([&] { load_audio_clip(root, wrong); });
        wrong = record;
        wrong.metadata["forge.audio"]["sample_rate"] = 1;
        rejects([&] { load_audio_clip(root, wrong); });
        // Actual existing audio engine consumes cooked data, with no device/second clock.
        EngineContext engine(WorldRole::Runtime, false,
                             {audio_module({root, AudioOutput::Offline, true})});
        Scene scene(engine.world());
        scene.replace({{"version", 1},
                       {"entities", Json::array({{{"id", "speaker"},
                                                  {"name", "Speaker"},
                                                  {"components",
                                                   {{"forge.audio_source",
                                                     {{"clip", original.id},
                                                      {"play_on_start", true},
                                                      {"loop", true},
                                                      {"gain", 1},
                                                      {"pitch", 1},
                                                      {"spatialized", false},
                                                      {"minimum_distance", 1},
                                                      {"maximum_distance", 100}}}}}}})}});
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.tick(1.f / 60);
        auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
        audio->paused(false);
        std::vector<float> output(512);
        audio->read_offline(output);
        check(std::any_of(output.begin(), output.end(), [](float sample) { return sample != 0; }),
              "Existing miniaudio engine did not play selected cooked clip");
        // A good later source publishes a new immutable selection with the same
        // logical identity. The old copied/playing revision remains intact.
        const auto replacement = wav(2, 24000);
        write(root / request.source, replacement);
        auto next_plan = importer->discover(request, {});
        auto stale = candidate(next_plan, cook(next_plan));
        write(root / request.source, bytes);
        rejects([&] {
            publisher.publish(std::move(stale), *importer, [](const auto&, const auto&) {});
        });
        check(read_bytes(root / "forge.assets.json", max_asset_index_bytes) == good_index,
              "Stale audio candidate changed catalog");
        write(root / request.source, replacement);
        auto newer = publisher.publish(candidate(next_plan, cook(next_plan)), *importer,
                                       [](const auto&, const auto&) {});
        const auto next_record = newer.catalog.records().at(original.id);
        check(next_record.id == original.id &&
                  next_record.metadata.at("forge.import").at("generation") == 2 &&
                  load_audio_clip(root, next_record).channels == 2 &&
                  load_audio_clip(root, next_record).rate == 24000 && loaded.channels == 1 &&
                  loaded.rate == 48000,
              "Audio reimport changed identity or old revision contents");
        const auto cooked_path = root / ".forge/cache/derived" / newer.artifact.key / "audio.fpcm";
        auto cooked_bytes = read_bytes(cooked_path, max_audio_pcm_bytes + 32);
        const auto good_bytes = cooked_bytes;
        cooked_bytes.back() ^= std::byte{1};
        write(cooked_path, cooked_bytes);
        rejects([&] { load_audio_clip(root, next_record); });
        write(cooked_path, good_bytes);
        AssetFileTransaction transaction(lease);
        const auto rewrite = project_asset_file_rewriter(root);
        auto move = prepare_asset_file_operation(
            root, {AssetFileAction::Move, original.id, "Assets/moved.wav"}, rewrite);
        transaction.commit(move.changes, false);
        auto moved = AssetCatalog::open_project(root).records().at(original.id);
        check(moved.source == "Assets/moved.wav" &&
                  load_audio_clip(root, moved).pcm == load_audio_clip(root, next_record).pcm &&
                  !std::filesystem::exists(root / "Assets/tone.wav"),
              "Audio move changed identity or cooked selection");
        auto copy = prepare_asset_file_operation(
            root, {AssetFileAction::Duplicate, original.id, "Assets/copied.wav"}, rewrite);
        transaction.commit(copy.changes, false);
        const auto copied = AssetCatalog::open_project(root).records().at(copy.result);
        check(copy.result != original.id && !copied.metadata.contains("forge.import") &&
                  !copied.metadata.contains("forge.audio") &&
                  AssetImportSidecar::parse(
                      *asset_storage::read(root / "Assets/copied.wav.forge-import.json"))
                          .identity.owner == copy.result,
              "Copied audio retained the original identity or admission claim");
        request.asset = copy.result;
        request.source = "Assets/copied.wav";
        plan = importer->discover(request, {});
        const auto copied_publication = publisher.publish(candidate(plan, cook(plan)), *importer,
                                                          [](const auto&, const auto&) {});
        check(
            load_audio_clip(root, copied_publication.catalog.records().at(copy.result)).channels ==
                2,
            "Copied audio cannot be reimported under its new identity");
        // Publish processing settings on the normal transaction, then export only
        // the selected immutable clip. The original WAV and prior revision survive.
        const auto source_before = read_bytes(root / request.source, max_audio_source_bytes);
        request.settings = audio_settings().edit(request.settings, "force_mono", true);
        request.settings = audio_settings().edit(request.settings, "normalize_peak", true);
        const auto processed_plan = importer->discover(request, {});
        check(processed_plan.input.key() != plan.input.key(),
              "Processing did not change the canonical DDC key");
        const auto processed = publisher.publish(candidate(processed_plan, cook(processed_plan)),
                                                 *importer, [](const auto&, const auto&) {});
        const auto processed_record = processed.catalog.records().at(copy.result);
        const auto processed_clip = load_audio_clip(root, processed_record);
        check(processed_clip.channels == 1 && processed_clip.rate == 24000 &&
                  processed_clip.frames == 480 &&
                  std::all_of(processed_clip.pcm.begin(), processed_clip.pcm.end(),
                              [](float x) { return x == 1; }) &&
                  read_bytes(root / request.source, max_audio_source_bytes) == source_before &&
                  load_audio_clip(root, next_record).channels == 2,
              "Processed publication changed source/previous selection or cooked samples");
        const auto exported =
            root.parent_path() / (AssetId::generate().str() + "-processed-export");
        const std::array roots{copy.result};
        prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
        package_runtime_content(root, exported, roots, {"windows", "d3d12"}, {}, {},
                                scene.schema());
        const auto relocated = exported.string() + "-relocated";
        std::filesystem::rename(exported, relocated);
        std::filesystem::remove(root / request.source);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        const auto shipped_clip = load_audio_clip(relocated, shipped.records().at(copy.result));
        check(shipped_clip.pcm == processed_clip.pcm && shipped_clip.channels == 1,
              "Relocated export lost processed AudioClip PCM");
        EngineContext exported_engine(WorldRole::Runtime, false,
                                      {audio_module({relocated, AudioOutput::Offline, true})});
        Scene exported_scene(exported_engine.world());
        auto exported_document = scene.snapshot();
        for (auto& row : exported_document["entities"])
            if (row["components"].contains("forge.audio_source"))
                row["components"]["forge.audio_source"]["clip"] = copy.result;
        exported_scene.restore_snapshot(exported_document);
        RuntimeSimulation exported_simulation(exported_engine.world(), exported_scene);
        exported_simulation.sync_audio();
        exported_simulation.audio_paused(false);
        auto exported_audio =
            std::static_pointer_cast<AudioRuntime>(exported_engine.services().audio());
        exported_audio->read_offline(output);
        check(std::any_of(output.begin(), output.end(), [](float sample) { return sample > .1f; }),
              "Runtime did not play relocated processed PCM");
        std::filesystem::remove_all(relocated);
        std::cout << "WAV/PCM admission, identity, metadata, deterministic cook, DDC, last-good "
                     "and runtime output passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
