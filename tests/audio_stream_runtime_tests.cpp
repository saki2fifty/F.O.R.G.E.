#include "asset_bytes.hpp"
#include "audio_authoring.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
#include "audio_loading_test.hpp"
#include "audio_preview.hpp"
#include "audio_source_access.hpp"
#include <algorithm>
#include <cmath>
#include <forge/audio.hpp>
#include <forge/runtime.hpp>
#include <forge/sound_cue.hpp>
#include <fstream>
#include <functional>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void write_tone(const std::filesystem::path& path, unsigned channels) {
    constexpr unsigned frames = 240137, rate = 48000;
    std::ofstream out(path, std::ios::binary);
    auto integer = [&](std::uint32_t value, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            out.put(char(value >> (8 * i)));
    };
    out.write("RIFF", 4);
    integer(36 + frames * channels * 2, 4);
    out.write("WAVEfmt ", 8);
    integer(16, 4);
    integer(1, 2);
    integer(channels, 2);
    integer(rate, 4);
    integer(rate * channels * 2, 4);
    integer(channels * 2, 2);
    integer(16, 2);
    out.write("data", 4);
    integer(frames * channels * 2, 4);
    for (unsigned i = 0; i < frames; ++i)
        for (unsigned ch = 0; ch < channels; ++ch) {
            const double t = double(i) / rate;
            const double sample = (1 + .2 * std::sin(t * 3)) *
                                  (.16 * std::sin(t * 2 * 3.141592653589793 * (440 + ch * 173)) +
                                   .07 * std::sin(t * 2 * 3.141592653589793 * 1277));
            integer(std::uint16_t(std::int16_t(sample * 32767)), 2);
        }
    require(bool(out), "Cannot write streaming tone");
}
AssetRecord publish(const std::filesystem::path& root, std::string name, bool stream) {
    auto original = AssetCatalog::register_audio_clip(root, "Assets/" + name + ".wav");
    auto importer = audio_importer({});
    AssetImportRequest request{
        original.id, root, original.source, {"windows", "d3d12", "desktop"}, {"forge.audio.wav"}};
    if (stream)
        request.settings = audio_settings().edit(request.settings, "playback_storage", "stream");
    const auto plan = importer->discover(request, {});
    auto files = execute_audio_recipe(
        {{{"recipe", "forge.audio.wav"},
          {"revision", audio_recipe_revision()},
          {"source_digest", plan.input.source_digest},
          {"settings", request.settings}},
         {{"source.wav", read_bytes(root / request.source, max_audio_source_bytes)}}});
    ProjectLease lease(root);
    AssetPublisher publisher(lease);
    AssetPublicationCandidate candidate;
    candidate.ticket = publisher.capture(request.asset, request.source);
    candidate.input = plan.input;
    candidate.sidecar.settings = request.settings;
    candidate.sidecar.build_inputs = plan.input.document();
    candidate.files = std::move(files);
    prepare_audio_publication(candidate, plan);
    return publisher.publish(std::move(candidate), *importer, [](const auto&, const auto&) {})
        .catalog.records()
        .at(original.id);
}
Json document(AssetId clip, float pitch = 1, bool loop = true, unsigned count = 1) {
    auto rows = Json::array();
    for (unsigned i = 0; i < count; ++i)
        rows.push_back({{"id", "speaker" + std::to_string(i)},
                        {"name", "Speaker"},
                        {"components",
                         {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 10}}},
                          {"forge.audio_source",
                           {{"clip", clip},
                            {"pitch", pitch},
                            {"loop", loop},
                            {"spatialized", false},
                            {"play_on_start", true},
                            {"gain", 1},
                            {"minimum_distance", 1},
                            {"maximum_distance", 100}}}}}});
    rows.push_back({{"id", "listener"},
                    {"name", "Listener"},
                    {"components",
                     {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                      {"forge.audio_listener", {{"enabled", true}}}}}});
    return {{"version", 1}, {"entities", rows}};
}
struct Running {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AudioRuntime> audio;
    Running(const std::filesystem::path& root, const Json& doc, AudioRouting routing = {})
        : engine(
              WorldRole::Runtime, false,
              {audio_module({root, AudioOutput::Offline, true, 1, {1, 1, 1}, std::move(routing)})}),
          scene(engine.world()), simulation(engine.world(), scene),
          audio(std::static_pointer_cast<AudioRuntime>(engine.services().audio())) {
        scene.restore_snapshot(doc);
        simulation.reset_presentation();
        simulation.sync_audio();
    }
    void prepared() {
        require(audio->wait_for_sources(std::chrono::seconds(5)), "Runtime stream did not prepare");
        audio->paused(false);
    }
    EntityRef ref(unsigned i = 0) const { return scene.reference("speaker" + std::to_string(i)); }
    void control(AudioSourceOperation op) { AudioSourceAccess::control(*audio, ref(), op); }
    std::array<float, 512> read() {
        std::array<float, 512> pcm{};
        audio->read_offline(pcm);
        return pcm;
    }
    void set(std::function<void(AudioSource&)> change) {
        auto entity = scene.entity("speaker0");
        auto value = entity.get<AudioSource>();
        change(value);
        entity.set(value);
        audio->refresh_sources();
    }
};
void compare(Running& streamed, Running& resident, unsigned quanta, const std::string& context) {
    for (unsigned q = 0; q < quanta; ++q) {
        const auto a = streamed.read(), b = resident.read();
        for (unsigned i = 0; i < a.size(); ++i)
            if (!std::isfinite(a[i]) || std::abs(a[i] - b[i]) >= 1e-6f)
                require(false, context + " quantum=" + std::to_string(q) + " sample=" +
                                   std::to_string(i) + " actual=" + std::to_string(a[i]) +
                                   " expected=" + std::to_string(b[i]));
    }
}
void timed_cue(const std::filesystem::path& root, AssetId stream, AssetId resident) {
    auto a = document(stream, 2, false), b = document(resident, 2, false);
    for (auto* value : {&a, &b}) {
        (*value)["entities"][0]["components"]["forge.audio_source"]["play_on_start"] = false;
        (*value)["entities"].push_back({{"id", "cue"}, {"name", "Delayed stream"},
            {"components", {{"forge.sound_cue", sound_cue_json({})}}}});
    }
    Running actual(root, a), expected(root, b);
    actual.prepared();
    expected.prepared();
    actual.scene.entity("cue").set(SoundCue{true, 1, 0, {{0, actual.ref(), {}, 4096. / 48000}}});
    AudioSourceAccess::control(*actual.audio, actual.scene.reference("cue"), AudioSourceOperation::OneShot);
    actual.control(AudioSourceOperation::Pause);
    require(actual.audio->wait_for_loading(std::chrono::seconds(5)), "Delayed stream did not prepare");
    for (unsigned i = 0; i < 8; ++i) {
        const auto pcm = actual.read();
        require(std::ranges::all_of(pcm, [](float x) { return x == 0; }),
                "A paused preparing delayed stream played early");
    }
    actual.control(AudioSourceOperation::Resume);
    expected.control(AudioSourceOperation::OneShot);
    for (unsigned i = 0; i < 16; ++i) {
        const auto pcm = actual.read();
        require(std::ranges::all_of(pcm, [](float x) { return x == 0; }),
                "A delayed stream advanced before its pitch-independent output deadline");
    }
    compare(actual, expected, 80, "Delayed prepared stream versus ordinary resident one-shot");
}
void custom_routing(const std::filesystem::path& root, AssetId stream, AssetId resident) {
    const AudioRouting routing{
        {{"rain", "", .5f, "effects", {{"music", .25f}}}, {"dry", "", 1, "master", {}}}};
    auto streamed_doc = document(stream), resident_doc = document(resident);
    streamed_doc["entities"][0]["components"]["forge.audio_source"]["custom_bus"] = "rain";
    resident_doc["entities"][0]["components"]["forge.audio_source"]["gain"] = .3125f;
    Running actual(root, streamed_doc, routing), expected(root, resident_doc);
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"rain", .25f}, {"dry", 1}});
    actual.prepared();
    expected.prepared();
    compare(actual, expected, 80, "Custom fader admitted during stream preparation");
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"rain", .5f}, {"dry", 1}});
    expected.set([](auto& value) { value.gain = .625f; });
    compare(actual, expected, 80, "Stream custom send PCM");
    actual.audio->mix_volumes(1, {.25f, .5f, 1});
    expected.set([](auto& value) { value.gain = .28125f; });
    compare(actual, expected, 80, "Stream custom terminal fader PCM");
    actual.set([](auto& value) { value.custom_bus = "dry"; });
    expected.set([](auto& value) { value.gain = 1; });
    compare(actual, expected, 80, "Stream custom reroute cursor continuity");
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"rain", .125f}, {"dry", .25f}});
    expected.set([](auto& value) { value.gain = .25f; });
    actual.control(AudioSourceOperation::OneShot);
    expected.control(AudioSourceOperation::OneShot);
    actual.prepared();
    expected.prepared();
    compare(actual, expected, 80, "Stream custom one-shot output");
    require(actual.audio->status().at("stream_voices") == 2 &&
                actual.audio->status().at("decoded_bytes") == 0,
            "Custom one-shot fixture did not use owned streamed voices");
    for (auto* world : {&actual, &expected})
        world->audio->paused(true);
    compare(actual, expected, 8, "Custom stream full suspension");
    for (auto* world : {&actual, &expected})
        world->audio->paused(false);
    compare(actual, expected, 80, "Custom stream resume PCM");
}
void playback(const std::filesystem::path& root, AssetId stream, AssetId resident) {
    for (bool loop : {false, true})
        for (float pitch : {.5f, 1.f, 2.f}) {
            Running actual(root, document(stream, pitch, loop));
            Running expected(root, document(resident, pitch, loop));
            actual.prepared();
            expected.prepared();
            const auto state = actual.audio->status();
            require(state.at("decoded_bytes") == 0 && state.at("stream_voices") == 1 &&
                        state.at("stream_backing_files") == 1 && state.at("stream_page_bytes") > 0,
                    "Stream accounting charged full PCM or lost backing/pages");
            compare(actual, expected, 180, "Initial/page PCM");
            actual.control(AudioSourceOperation::Pause);
            expected.control(AudioSourceOperation::Pause);
            compare(actual, expected, 3, "Paused PCM");
            actual.control(AudioSourceOperation::Resume);
            expected.control(AudioSourceOperation::Resume);
            compare(actual, expected, 1450, "Resumed/loop/EOS PCM pitch=" + std::to_string(pitch));
        }
    Running actual(root, document(stream)), expected(root, document(resident));
    actual.prepared();
    expected.prepared();
    compare(actual, expected, 40, "Live controls setup");
    actual.set([](auto& v) {
        v.pitch = 1.7f;
        v.low_pass_enabled = true;
        v.low_pass_hz = 3100;
    });
    expected.set([](auto& v) {
        v.pitch = 1.7f;
        v.low_pass_enabled = true;
        v.low_pass_hz = 3100;
    });
    compare(actual, expected, 1100, "Live pitch/filter PCM");
    actual.set([](auto& v) { v.loop = false; });
    expected.set([](auto& v) { v.loop = false; });
    compare(actual, expected, 1500, "Live loop disable PCM");
}
void spatial(const std::filesystem::path& root, AssetId stream, AssetId resident) {
    Running actual(root, document(stream, 4)), expected(root, document(resident, 4));
    actual.prepared();
    expected.prepared();
    auto configure = [](auto& v) {
        v.spatialized = true;
        v.rolloff = 0;
        v.doppler = 2;
        v.cone_enabled = true;
        v.inner_angle = 90;
        v.outer_angle = 180;
        v.echo_enabled = true;
        v.echo_seconds = .05f;
        v.echo_feedback = .2f;
        v.reverb_enabled = true;
        v.reverb_decay = .5f;
        v.reverb_wet = .1f;
    };
    actual.set(configure);
    expected.set(configure);
    for (auto* running : {&actual, &expected}) {
        // Extreme approach exercises the existing Doppler safety bound with
        // maximum authored pitch, rather than weakening the offline page gate.
        running->scene.entity("speaker0").set<LocalTranslation>({0, 0, -1000});
        running->simulation.tick(1.f / 60);
    }
    compare(actual, expected, 1100, "Spatial/cone/Doppler/effects PCM");
    require(actual.audio->status().at("stream_underruns") == 0,
            "Offline spatial rendering starved native stream pages");
}
void immutable_lifetime(const std::filesystem::path& root, AssetId stream, AssetId resident) {
    Running actual(root, document(stream)), expected(root, document(resident));
    actual.prepared();
    expected.prepared();
    compare(actual, expected, 20, "Before source/cache removal");
    struct Hidden {
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> paths;
        ~Hidden() {
            for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
                std::error_code error;
                std::filesystem::rename(it->second, it->first, error);
            }
        }
        void hide(const std::filesystem::path& path) {
            if (!std::filesystem::exists(path))
                return;
            const auto hidden = path.parent_path() / (path.filename().string() + "-hidden");
            std::filesystem::rename(path, hidden);
            paths.emplace_back(path, hidden);
        }
    } hidden;
    hidden.hide(root / "Assets");
    hidden.hide(root / ".forge");
    compare(actual, expected, 1400, "Owned stream after source/cache removal");
}
void transport(const std::filesystem::path& root, AssetId stream, AssetId resident) {
    Running actual(root, document(stream));
    actual.audio->stop_source(actual.ref());
    actual.prepared();
    for (float value : actual.read())
        require(value == 0, "Pending Stop leaked autoplay");
    actual.control(AudioSourceOperation::Restart);
    actual.control(AudioSourceOperation::Pause);
    actual.prepared();
    require(AudioSourceAccess::status(*actual.audio, actual.ref()).at("source_paused") == true,
            "Pending restart/pause lost intent");
    actual.control(AudioSourceOperation::Resume);
    Running fresh(root, document(resident));
    fresh.prepared();
    compare(actual, fresh, 1100, "Restart from zero");
    actual.control(AudioSourceOperation::OneShot);
    actual.audio->poll_loading();
    actual.prepared();
    require(actual.audio->status().at("one_shots") == 1 &&
                actual.audio->status().at("stream_voices") == 2,
            "Pending one-shot did not prepare");
    actual.control(AudioSourceOperation::Restart);
    actual.control(AudioSourceOperation::Pause);
    for (unsigned i = 0; i < 4; ++i)
        (void)actual.read();
    const auto paused_pcm = actual.read();
    require(std::ranges::all_of(paused_pcm, [](float value) { return value == 0; }),
            "Pause during primary restart left one-shot PCM playing");
    require(AudioSourceAccess::status(*actual.audio, actual.ref()).at("paused_one_shots") == 1,
            "Pause during primary restart did not pause its existing one-shot");
    actual.prepared();
    require(AudioSourceAccess::status(*actual.audio, actual.ref()).at("paused_one_shots") == 1,
            "Primary preparation lost one-shot pause");
    actual.control(AudioSourceOperation::Resume);
    require(AudioSourceAccess::status(*actual.audio, actual.ref()).at("playing_one_shots") == 1,
            "Resume after primary preparation did not resume its one-shot");
    actual.control(AudioSourceOperation::Stop);
    actual.prepared();
    require(actual.audio->status().at("one_shots") == 0, "Stop retained streamed one-shot");
    Running bounded(root, document(stream, 1, true, 9));
    require(!bounded.audio->wait_for_sources(std::chrono::seconds(5)) &&
                bounded.audio->status().at("failed_sources") == 1 &&
                bounded.audio->status().at("reserved_streams") == 8,
            "Native stream quota was not enforced");
    actual.scene.delete_subtree("speaker0");
    actual.audio->refresh_sources();
    require(actual.audio->status().at("voices") == 0, "Deleted stream kept a voice");
}
void preview(const std::filesystem::path& root, const AssetRecord& stream,
             const AssetRecord& resident) {
    AudioClipPreview actual, expected;
    actual.configure(true, 1);
    expected.configure(true, 1);
    actual.start(root, stream, AudioOutput::Offline);
    expected.start(root, resident, AudioOutput::Offline);
    await_audio_preview(actual);
    await_audio_preview(expected);
    for (double seconds : {2.75, .125, 4.99, .75}) {
        actual.seek(seconds);
        expected.seek(seconds);
        require(actual.pending() && std::abs(actual.position() - seconds) < 1. / 48000,
                "Asynchronous preview seek lost its target");
        await_audio_preview(actual);
        await_audio_preview(expected);
        std::array<float, 512> a{}, b{};
        for (unsigned q = 0; q < 240; ++q) {
            actual.read_offline(a);
            expected.read_offline(b);
            for (unsigned i = 0; i < a.size(); ++i)
                if (a[i] != b[i])
                    require(false, "Preview PCM seek=" + std::to_string(seconds) + " quantum=" +
                                       std::to_string(q) + " sample=" + std::to_string(i) +
                                       " actual=" + std::to_string(a[i]) + " expected=" +
                                       std::to_string(b[i]) + " final=" + std::to_string(a.back()) +
                                       " position=" + std::to_string(actual.position()));
        }
    }
    actual.paused(true);
    actual.seek(1.25);
    await_audio_preview(actual);
    require(actual.paused() && std::abs(actual.position() - 1.25) < 1. / 48000,
            "Stream seek lost paused transport");
    std::array<float, 512> silence{};
    actual.read_offline(silence);
    for (float value : silence)
        require(value == 0, "Paused stream seek emitted audio");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need private test scratch");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        for (unsigned channels : {1u, 2u}) {
            const auto name = "tone" + std::to_string(channels);
            write_tone(root / "Assets" / (name + ".wav"), channels);
            std::filesystem::copy_file(root / "Assets" / (name + ".wav"),
                                       root / "Assets" / (name + "-resident.wav"));
            const auto streamed = publish(root, name, true);
            const auto resident = publish(root, name + "-resident", false);
            timed_cue(root, streamed.id, resident.id);
            custom_routing(root, streamed.id, resident.id);
            playback(root, streamed.id, resident.id);
            transport(root, streamed.id, resident.id);
            preview(root, streamed, resident);
            spatial(root, streamed.id, resident.id);
            immutable_lifetime(root, streamed.id, resident.id);
        }
        std::filesystem::remove_all(root);
        std::cout << "Runtime stream/resident PCM, live controls, pending commands, quotas and "
                     "preview passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
