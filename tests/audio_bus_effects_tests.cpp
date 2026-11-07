#include "audio_source_access.hpp"
#include "audio_test_fixture.hpp"
#include <forge/sound_cue.hpp>
#include <limits>

namespace {
Json speakers(AssetId clip, const AudioBusEffects& effects, bool routed) {
    auto doc = source(clip);
    auto& first = doc["entities"][0];
    auto& sound = first["components"]["forge.audio_source"];
    sound["gain"] = .3f;
    sound["custom_bus"] = routed ? "room" : "";
    if (!routed)
        sound.update(audio_bus_effects_json(effects));
    auto second = first;
    second["id"] = "second";
    second["name"] = "Independent second voice";
    second["components"]["forge.audio_source"]["pitch"] = 1.5f;
    second["components"]["forge.audio_source"]["pause_with_game"] = false;
    auto third = first;
    third["id"] = "third";
    third["name"] = "Second pausable voice";
    third["components"]["forge.audio_source"]["pitch"] = .75f;
    doc["entities"].push_back(second);
    doc["entities"].push_back(third);
    return doc;
}
void compare(Fixture& actual, Fixture& expected, unsigned blocks = 12, float scale = 1) {
    double energy = 0;
    for (unsigned block = 0; block < blocks; ++block) {
        std::array<float, 1024> a{}, b{};
        actual.audio->read_offline(a);
        expected.audio->read_offline(b);
        for (unsigned i = 0; i < a.size(); ++i) {
            if (!std::isfinite(a[i]) || std::abs(a[i] - b[i] * scale) >= 3e-6f)
                throw std::runtime_error("Shared bus PCM mismatch block=" + std::to_string(block) +
                                         " sample=" + std::to_string(i) +
                                         " actual=" + std::to_string(a[i]) +
                                         " expected=" + std::to_string(b[i] * scale));
            energy += double(b[i]) * b[i];
        }
    }
    check(energy > 1e-5, "Shared bus oracle did not exercise audible signal");
}
void values() {
    const auto boundary = audio_bus_effects_from_json(
        {{"reverb_decay", .1}, {"reverb_damping", .95}, {"echo_seconds", .01}});
    check(audio_bus_effects_from_json(audio_bus_effects_json(boundary)) == boundary,
          "Decimal boundary parameters or their native float roundtrip were rejected");
    AudioRouting legacy{{{"room", "Room", 1, "master", {}}}};
    check(!audio_routing_json(legacy)["buses"][0].contains("effects") &&
              legacy.buses[0].effects == AudioBusEffects{},
          "Legacy routing acquired effect settings");
    auto graph = legacy;
    graph.buses[0].effects.echo_seconds = .5f;
    check(graph.buses[0].effects != AudioBusEffects{} && !graph.buses[0].effects.active(),
          "Retained dormant bus parameters lost capability admission");
    graph.buses[0].effects.echo_enabled = true;
    check(audio_routing_from_json(audio_routing_json(graph)) == graph,
          "Bus effects roundtrip changed parameters");
    for (const auto& invalid :
         {Json(), Json::array(), Json{{"unknown", 1}}, Json{{"echo_enabled", 1}},
          Json{{"echo_seconds", true}}, Json{{"low_pass_hz", 20001}}, Json{{"high_pass_hz", 0}},
          Json{{"echo_seconds", 0}}, Json{{"echo_feedback", .751}}, Json{{"reverb_decay", 11}},
          Json{{"reverb_damping", .96}}, Json{{"reverb_wet", -1}}})
        reject([&] { (void)audio_bus_effects_from_json(invalid); });
    graph.buses[0].effects.echo_feedback = std::numeric_limits<float>::quiet_NaN();
    reject([&] { validate_audio_routing(graph); });
}
void linear_pcm(const std::filesystem::path& root, AssetId clip) {
    for (unsigned profile = 0; profile < 5; ++profile) {
        std::cout << "Linear profile " << profile << std::endl;
        AudioBusEffects fx;
        fx.low_pass_enabled = profile == 0 || profile == 4;
        fx.high_pass_enabled = profile == 1 || profile == 4;
        fx.low_pass_hz = 1200;
        fx.high_pass_hz = 120;
        fx.echo_enabled = profile == 2 || profile == 4;
        fx.echo_seconds = .01f;
        fx.echo_feedback = .5f;
        fx.reverb_enabled = profile == 3 || profile == 4;
        fx.reverb_decay = .1f;
        fx.reverb_wet = .3f;
        AudioRouting graph{{{"room", "Room", .65f, "master", {{"effects", .5f}}, fx}}};
        Fixture actual(root, 1, false, graph), oracle(root);
        actual.load(speakers(clip, fx, true));
        oracle.load(speakers(clip, fx, false));
        actual.audio->mix_volumes(1, {1, .75f, 1});
        actual.simulation.audio_paused(false);
        oracle.simulation.audio_paused(false);
        compare(actual, oracle, 12, .65f * (1 + .5f * .75f));
        actual.audio->game_paused(true);
        oracle.audio->game_paused(true);
        compare(actual, oracle, 12, .65f * (1 + .5f * .75f));
        actual.audio->game_paused(false);
        oracle.audio->game_paused(false);
        compare(actual, oracle, 12, .65f * (1 + .5f * .75f));
        const auto status = actual.audio->status();
        check(status.at("bus_processing") == true && status.at("bus_effect_chains") == 2 &&
                  status.at("voices") == 3 && status.at("source_voices") == 3,
              "Bus fanout duplicated source voices or effect domains");
    }
}
void shared_history(const std::filesystem::path& root, AssetId clip) {
    AudioBusEffects fx;
    fx.echo_enabled = true;
    fx.echo_seconds = .02f;
    fx.echo_feedback = .75f;
    AudioRouting graph{{{"room", "", 1, "master", {}, fx}}};
    Fixture actual(root, 1, false, graph), uninterrupted(root, 1, false, graph);
    auto doc = speakers(clip, fx, true);
    actual.load(doc);
    uninterrupted.load(doc);
    actual.simulation.audio_paused(false);
    uninterrupted.simulation.audio_paused(false);
    compare(actual, uninterrupted);
    const auto memory = actual.audio->status().at("bus_echo_buffer_bytes");
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"room", 0}});
    for (unsigned i = 0; i < 10; ++i) {
        (void)actual.energy();
        (void)uninterrupted.energy();
    }
    check(actual.energy() < 1e-12, "Muted bus leaked retained effect output");
    (void)uninterrupted.energy();
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"room", 1}});
    // Existing parent groups can retain two buffered frames at a control edge.
    // Advance both timelines equally before comparing the retained DSP history.
    (void)actual.energy(8);
    (void)uninterrupted.energy(8);
    compare(actual, uninterrupted);
    for (const char* id : {"speaker", "second", "third"}) {
        AudioSourceAccess::control(*actual.audio, actual.ref(id), AudioSourceOperation::Pause);
        check(AudioSourceAccess::status(*actual.audio, actual.ref(id)).at("source_paused") == true,
              "Source Pause did not hold the bus contributor");
    }
    check(actual.energy(2048) > 1e-6, "Source Pause erased already mixed bus history");
    for (const char* id : {"speaker", "second", "third"})
        AudioSourceAccess::control(*actual.audio, actual.ref(id), AudioSourceOperation::Resume);
    check(AudioSourceAccess::status(*actual.audio, actual.ref()).at("playing") == true,
          "Bus-routed Source Resume did not retain its cursor");
    actual.audio->stop_source(actual.ref());
    actual.audio->stop_source(actual.ref("second"));
    actual.audio->stop_source(actual.ref("third"));
    actual.audio->poll_loading();
    check(actual.energy(2048) > 1e-6, "Source Stop erased already mixed shared bus history");
    for (const char* id : {"speaker", "second", "third"})
        actual.scene.entity(id).remove<AudioSource>();
    actual.audio->refresh_sources();
    check(actual.audio->status().at("voices") == 0,
          "Shared tail retained retired source voice slots");
    actual.audio->game_paused(true);
    check(actual.energy(2048) > 1e-8,
          "Independent bus history stopped when its source stopped during game pause");
    actual.simulation.suspend_audio();
    check(actual.audio->status().at("suspended") == true,
          "Full suspension fixture did not invoke the suspension owner");
    (void)actual.energy(8);
    check(actual.energy(1024) == 0, "Full suspension did not freeze all bus output");
    check(actual.audio->status().at("bus_echo_buffer_bytes") == memory,
          "Shared bus memory scaled with voice controls");
}
void downstream_history(const std::filesystem::path& root, AssetId clip) {
    AudioBusEffects reverb;
    reverb.reverb_enabled = true;
    reverb.reverb_decay = .4f;
    reverb.reverb_wet = 1;
    const AudioRouting graph{{{"feed", "", 1, "room", {}}, {"room", "", 1, "master", {}, reverb}}};
    Fixture actual(root, 1, false, graph);
    auto doc = speakers(clip, {}, true);
    for (auto& row : doc["entities"])
        row["components"]["forge.audio_source"]["custom_bus"] = "feed";
    actual.load(doc);
    actual.audio->paused(false);
    check(actual.energy(12000) > 1e-6, "Downstream history fixture did not excite reverberation");
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"feed", 0}, {"room", 1}});
    (void)actual.energy(8);
    check(actual.energy(2048) > 1e-7, "Upstream mute erased downstream shared history");
    actual.audio->mix_volumes(1, {1, 1, 1}, {{"feed", 0}, {"room", 0}});
    (void)actual.energy(8);
    check(actual.energy(2048) == 0, "Downstream fader did not silence its own retained history");
}
void nested_pcm(const std::filesystem::path& root, AssetId clip) {
    AudioBusEffects echo, combined;
    echo.echo_enabled = true;
    echo.echo_seconds = .01f;
    echo.echo_feedback = .5f;
    combined = echo;
    combined.low_pass_enabled = true;
    combined.low_pass_hz = 900;
    AudioBusEffects filter;
    filter.low_pass_enabled = true;
    filter.low_pass_hz = 900;
    const AudioRouting graph{{{"room", "", .75f, "filtered", {{"master", .25f}}, echo},
                              {"filtered", "", .5f, "master", {}, filter}}};
    Fixture actual(root, 1, false, graph), oracle(root);
    actual.load(speakers(clip, {}, true));
    auto document = speakers(clip, combined, false);
    auto dry_branch = speakers(clip, echo, false);
    for (auto& row : document["entities"])
        row["components"]["forge.audio_source"]["gain"] = .3f * .75f * .5f;
    for (auto row : dry_branch["entities"]) {
        row["id"] = row["id"].get<std::string>() + "_dry";
        row["components"]["forge.audio_source"]["gain"] = .3f * .75f * .25f;
        document["entities"].push_back(std::move(row));
    }
    oracle.load(document);
    actual.audio->paused(false);
    oracle.audio->paused(false);
    // Fixed linear filters/echo commute; this oracle enumerates the two paths
    // using ordinary source effects, without the production bus projection.
    compare(actual, oracle, 40);
    check(actual.audio->status().at("voices") == 3 &&
              actual.audio->status().at("bus_effect_chains") == 4,
          "Nested fanout duplicated native source cursors");
}
void completion_and_bounds(const std::filesystem::path& root, AssetId clip) {
    AudioBusEffects fx;
    fx.echo_enabled = true;
    fx.echo_seconds = 1;
    fx.echo_feedback = .75f;
    Fixture cue(root, 1, false, {{{"room", "", 1, "master", {}, fx}}});
    auto document = source(clip);
    auto& value = document["entities"][0]["components"]["forge.audio_source"];
    value["custom_bus"] = "room";
    value["play_on_start"] = value["loop"] = false;
    document["entities"].push_back({{"id", "cue"},
                                    {"name", "Completion producer"},
                                    {"components", {{"forge.sound_cue", sound_cue_json({})}}}});
    cue.load(document);
    SoundCue definition{true, 19, 0, {{0, cue.ref(), {}}}};
    definition.repeat_mode = 1;
    cue.scene.entity("cue").set(definition);
    AudioSourceAccess::control(*cue.audio, cue.ref("cue"), AudioSourceOperation::Restart);
    cue.audio->paused(false);
    (void)cue.energy(36000);
    check(AudioSourceAccess::status(*cue.audio, cue.ref("cue")).at("accepted_triggers") == 1,
          "Bus-routed completion ended before its Source voice");
    (void)cue.energy(24000);
    const auto status = AudioSourceAccess::status(*cue.audio, cue.ref("cue"));
    check(status.at("accepted_triggers") == 2 && status.at("repeat_completed_events") == 1,
          "Shared bus tails incorrectly held Source event completion");
    AudioSourceAccess::control(*cue.audio, cue.ref("cue"), AudioSourceOperation::Stop);

    AudioRouting maximum;
    fx.reverb_enabled = true;
    for (unsigned i = 0; i < audio_custom_bus_limit; ++i)
        maximum.buses.push_back({"bus_" + std::to_string(i), "", 1, "master", {}, fx});
    Fixture bounded(root, 1, false, maximum);
    const auto memory = bounded.audio->status();
    check(memory.at("bus_effect_chains") == 32 && memory.at("voices") == 0 &&
              memory.at("bus_echo_buffer_bytes") == 32 * 48000 * 2 * sizeof(float) &&
              memory.at("bus_reverb_buffer_bytes") == 32 * 59520,
          "Bounded bus population/memory does not match its actual owner count");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Test path required");
        values();
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        const auto clip = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        linear_pcm(root, clip.id);
        nested_pcm(root, clip.id);
        shared_history(root, clip.id);
        downstream_history(root, clip.id);
        completion_and_bounds(root, clip.id);
        std::cout << "Shared bus values, independent linear PCM, pause domains, faders and history "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
