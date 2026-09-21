#include "asset_bytes.hpp"
#include "asset_file_operations.hpp"
#include "asset_storage.hpp"
#include "audio_authoring.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
#include "audio_selection.hpp"
#include <forge/audio.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
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
        auto bytes = wav();
        write(root / "Assets/tone.wav", bytes);
        // Explicitly upgrade an existing registered record without changing ID.
        const auto original = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        auto importer =
            audio_importer(direct ? std::filesystem::path{} : std::filesystem::absolute(argv[2]));
        AssetImportRequest request{
            original.id, root, "Assets/tone.wav", {"portable", "none", "cpu"}, {"forge.audio.wav"}};
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
        Module legacy;
        RuntimeSimulation simulation(engine.world(), scene, legacy);
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
        std::cout << "WAV/PCM admission, identity, metadata, deterministic cook, DDC, last-good "
                     "and runtime output passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
