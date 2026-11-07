#include <algorithm>
#include <cmath>
#include <forge/audio_routing.hpp>
#include <forge/game_settings.hpp>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& work) {
    bool rejected = false;
    try {
        work();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid audio routing was admitted");
}
// Independent path enumeration, instead of the production terminal projection.
std::array<long double, 4> paths(const AudioRouting& routing, std::string_view key,
                                 const AudioBusVolumes& volumes, bool potential = false) {
    const std::array<std::string_view, 4> names{"master", "music", "effects", "dialogue"};
    for (unsigned i = 0; i < 4; ++i)
        if (key == names[i]) {
            std::array<long double, 4> terminal{};
            terminal[i] = 1;
            return terminal;
        }
    const auto& bus = *std::find_if(routing.buses.begin(), routing.buses.end(),
                                    [&](const auto& bus) { return bus.key == key; });
    auto result = paths(routing, bus.output, volumes, potential);
    for (const auto& send : bus.sends) {
        const auto branch = paths(routing, send.target, volumes, potential);
        for (unsigned i = 0; i < 4; ++i)
            result[i] += send.gain * branch[i];
    }
    const auto override = volumes.find(bus.key);
    const float volume = potential ? 1 : override == volumes.end() ? bus.volume : override->second;
    for (auto& value : result)
        value *= volume;
    return result;
}
void examples() {
    const std::array channels{.25f, .5f, .75f};
    const auto empty = AudioRoutingProjection::compile({});
    check(empty.buses().empty() && empty.volumes().empty(), "Empty graph acquired custom buses");
    for (unsigned i = 0; i < 4; ++i)
        check(empty.gain("", i, channels) == (i ? channels[i - 1] : 1),
              "Empty routing changed stock channel semantics");
    AudioRouting routing{{{"rain", "Rain", .5f, "effects", {{"music", .25f}}},
                          {"room", "Café room", .75f, "rain", {{"dialogue", .5f}}}}};
    auto projection = AudioRoutingProjection::compile(routing);
    check(projection.buses().at(0).terminals == std::array<double, 4>{0, .125, .5, 0} &&
              projection.buses().at(1).terminals == std::array<double, 4>{0, .09375, .375, .375},
          "Nested path weights lost source faders or post-fader sends");
    check(projection.gain("room", 0, channels) == .4921875f,
          "Custom routing did not sum independently faded terminal paths");
    check(audio_routing_from_json(audio_routing_json(routing)) == routing,
          "Typed routing roundtrip changed keys, labels, order or values");
    const auto before = routing;
    const auto muted = AudioRoutingProjection::compile(routing, {{"rain", 0}});
    check(muted.gain("rain", 0, channels) == 0 && muted.gain("room", 0, channels) == .28125f &&
              routing == before,
          "Partial live gains changed defaults or removed a separate send");
    reject([&] { AudioRoutingProjection::compile(routing, {{"room", 1}}, true); });
    reject([&] { AudioRoutingProjection::compile(routing, {{"unknown", .5f}}); });
    reject([&] { projection.gain("missing", 0, channels); });
    reject([&] { projection.gain("room", 4, channels); });
    reject([&] { projection.gain("room", 0, {1, 1, 2}); });
    for (float bad : {-1.f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity()}) {
        reject([&] { AudioRoutingProjection::compile(routing, {{"rain", bad}}); });
        auto invalid = routing;
        invalid.buses[0].volume = bad;
        reject([&] { validate_audio_routing(invalid); });
        invalid = routing;
        invalid.buses[0].sends[0].gain = bad;
        reject([&] { validate_audio_routing(invalid); });
    }
    auto invalid = routing;
    invalid.buses[0].sends.push_back({"effects", 0});
    reject([&] { validate_audio_routing(invalid); });
    invalid = routing;
    invalid.buses[0].output = "room";
    invalid.buses[0].volume = 0;
    reject([&] { validate_audio_routing(invalid); });
    invalid = routing;
    invalid.buses[0].sends.push_back({"room", 0});
    reject([&] { validate_audio_routing(invalid); });
    for (const char* key :
         {"master", "music", "effects", "dialogue", "Upper", "_name", "9name", "", "café"}) {
        invalid = routing;
        invalid.buses[0].key = key;
        reject([&] { validate_audio_routing(invalid); });
    }
    invalid = routing;
    invalid.buses[1].key = "rain";
    reject([&] { validate_audio_routing(invalid); });
    invalid = routing;
    invalid.buses[1].output = "missing";
    reject([&] { validate_audio_routing(invalid); });
    invalid = routing;
    invalid.buses[0].label = std::string("bad\0label", 9);
    reject([&] { validate_audio_routing(invalid); });
    invalid.buses[0].label = std::string("\xff", 1);
    reject([&] { validate_audio_routing(invalid); });
    invalid.buses[0].label = std::string(129, 'a');
    reject([&] { validate_audio_routing(invalid); });
    AudioRouting maximum;
    for (unsigned i = 0; i < 16; ++i)
        maximum.buses.push_back({"bus_" + std::to_string(i), "", 1, "master", {}});
    check(AudioRoutingProjection::compile(maximum).buses().size() == 16,
          "Sixteen custom buses were rejected");
    maximum.buses.push_back({"too_many", "", 1, "master", {}});
    reject([&] { validate_audio_routing(maximum); });
    AudioRouting amplified{{{"a", "", 0, "master", {{"music", 1}, {"effects", 1}, {"dialogue", 1}}},
                            {"b", "", 0, "a", {{"master", 1}, {"music", 1}, {"effects", 1}}},
                            {"c", "", 0, "b", {{"a", 1}, {"master", 1}, {"music", 1}}},
                            {"d", "", 0, "c", {{"a", 1}, {"b", 1}, {"master", 1}}}}};
    reject([&] { validate_audio_routing(amplified); }); // Potential25, despite muted defaults.
    auto data = audio_routing_json(routing);
    for (const auto& bad : {Json{{"version", 2}, {"buses", Json::array()}},
                            Json{{"version", 1.0}, {"buses", Json::array()}},
                            Json{{"version", 1}, {"buses", Json::array()}, {"unknown", 1}}})
        reject([&] { (void)audio_routing_from_json(bad); });
    data["buses"][0]["sends"][0]["gain"] = true;
    reject([&] { (void)audio_routing_from_json(data); });
    reject([&] { (void)audio_bus_volumes_from_json({{"rain", true}}); });
    check(audio_bus_volumes_from_json({{"rain", .25}, {"room", .75}}) ==
              AudioBusVolumes{{"rain", .25f}, {"room", .75f}},
          "Bus volume JSON changed values");
}
void generated() {
    std::mt19937 random(0x412b8a);
    const std::array<std::string, 4> terminals{"master", "music", "effects", "dialogue"};
    unsigned accepted = 0, rejected = 0;
    for (unsigned sample = 0; sample < 300; ++sample) {
        AudioRouting graph;
        AudioBusVolumes live;
        for (unsigned i = 0; i < 8; ++i) {
            std::vector<std::string> targets(terminals.begin(), terminals.end());
            for (unsigned j = 0; j < i; ++j)
                targets.push_back("bus_" + std::to_string(j));
            std::shuffle(targets.begin(), targets.end(), random);
            AudioBusDefinition bus{
                "bus_" + std::to_string(i), "", float(random() % 5) / 4, targets[0], {}};
            for (unsigned j = 0, count = random() % 4; j < count; ++j)
                bus.sends.push_back({targets[j + 1], float(random() % 5) / 4});
            live.emplace(bus.key, float(random() % 5) / 4);
            graph.buses.push_back(std::move(bus));
        }
        bool safe = true;
        for (const auto& bus : graph.buses) {
            const auto potential = paths(graph, bus.key, {}, true);
            long double sum = 0;
            for (auto value : potential)
                sum += value;
            safe = safe && sum <= 16;
        }
        if (!safe) {
            reject([&] { validate_audio_routing(graph); });
            ++rejected;
            continue;
        }
        ++accepted;
        const auto projection = AudioRoutingProjection::compile(graph, live, true);
        const std::array<float, 3> channels{.25f, .5f, .75f};
        for (const auto& bus : projection.buses()) {
            const auto expected = paths(graph, bus.key, live);
            long double gain = expected[0];
            for (unsigned i = 0; i < 4; ++i) {
                check(bus.terminals[i] == double(expected[i]),
                      "Projected weights differ from independent path enumeration");
                if (i)
                    gain += expected[i] * channels[i - 1];
            }
            check(projection.gain(bus.key, 0, channels) == float(gain),
                  "Routed gain differs from independent path sum");
        }
        std::reverse(graph.buses.begin(), graph.buses.end());
        for (auto& bus : graph.buses)
            std::reverse(bus.sends.begin(), bus.sends.end());
        check(AudioRoutingProjection::compile(graph, live, true).buses() == projection.buses(),
              "Graph ordering changed the deterministic projection");
    }
    check(accepted > 0, "Generated graph admission had no positive cases");
    std::cout << "Independent routing cases: " << accepted << " admitted, " << rejected
              << " potential-bound rejections\n";
}
void settings() {
    auto defaults = default_game_settings("com.forge.audio-routing", "Audio routing");
    check(!defaults.at("audio").contains("routing") &&
              !defaults.at("audio").contains("bus_volumes") &&
              audio_bus_volumes(defaults.at("audio")).empty(),
          "Legacy defaults acquired routing fields");
    defaults["audio"]["routing"] = audio_routing_json(
        {{{"rain", "Rain", .5f, "effects", {}}, {"room", "Room", .75f, "rain", {}}}});
    validate_game_settings(defaults);
    const auto before = defaults;
    check(audio_bus_volumes(defaults.at("audio")) == AudioBusVolumes{{"rain", .5f}, {"room", .75f}},
          "Project bus volume defaults diverged from the graph");
    auto resolved = resolve_game_settings(defaults, {{"audio", {{"bus_volumes", {{"rain", .25}}}}}},
                                          InputMap{});
    check(audio_bus_volumes(resolved.at("audio")) ==
                  AudioBusVolumes{{"rain", .25f}, {"room", .75f}} &&
              defaults == before,
          "User bus faders lost defaults or changed project settings");
    defaults["audio"]["bus_volumes"] = {{"rain", .125}};
    resolved =
        resolve_game_settings(defaults, {{"audio", {{"bus_volumes", {{"room", .5}}}}}}, InputMap{});
    check(audio_bus_volumes(resolved.at("audio")) ==
              AudioBusVolumes{{"rain", .125f}, {"room", .5f}},
          "Partial user gains erased an admitted project gain override");
    reject([&] {
        resolve_game_settings(defaults, {{"audio", {{"bus_volumes", {{"unknown", 1}}}}}},
                              InputMap{});
    });
    reject([&] {
        resolve_game_settings(
            defaults, {{"audio", {{"routing", defaults.at("audio").at("routing")}}}}, InputMap{});
    });
    reject([&] {
        resolve_game_settings(defaults, {{"audio", {{"bus_volumes", {{"rain", false}}}}}},
                              InputMap{});
    });
}
} // namespace
int main() {
    try {
        examples();
        generated();
        settings();
        std::cout << "Custom audio routing values, paths, limits, cycles, JSON and gain projection "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
