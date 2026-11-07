#include <algorithm>
#include <cmath>
#include <forge/audio_routing.hpp>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>

namespace forge {
namespace {
constexpr std::array<std::string_view, 4> terminal_names{"master", "music", "effects", "dialogue"};
void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error("Audio routing: " + std::string(message));
}
std::optional<unsigned> terminal(std::string_view name) noexcept {
    for (unsigned i = 0; i < terminal_names.size(); ++i)
        if (terminal_names[i] == name)
            return i;
    return {};
}
void volume(float value) {
    require(std::isfinite(value) && value >= 0 && value <= 1,
            "volumes and send gains must be finite and between zero and one");
}
float read_volume(const nlohmann::json& value) {
    require(value.is_number(), "volumes and send gains must be numeric");
    const double number = value.get<double>();
    require(std::isfinite(number) && number >= 0 && number <= 1,
            "volumes and send gains must be finite and between zero and one");
    return float(number);
}
void fields(const nlohmann::json& value, std::initializer_list<std::string_view> allowed) {
    require(value.is_object(), "expected an object");
    for (const auto& [name, item] : value.items()) {
        (void)item;
        require(std::find(allowed.begin(), allowed.end(), name) != allowed.end(),
                "unsupported field " + name);
    }
}
std::string text(const nlohmann::json& value, std::size_t maximum) {
    require(value.is_string(), "keys, targets and labels must be text");
    const auto& result = value.get_ref<const std::string&>();
    require(result.size() <= maximum, "routing text exceeds its byte limit");
    return result;
}
constexpr std::array effect_flags{
    std::pair{"low_pass_enabled", &AudioBusEffects::low_pass_enabled},
    std::pair{"high_pass_enabled", &AudioBusEffects::high_pass_enabled},
    std::pair{"echo_enabled", &AudioBusEffects::echo_enabled},
    std::pair{"reverb_enabled", &AudioBusEffects::reverb_enabled}};
struct EffectParameter {
    const char* name;
    float AudioBusEffects::* member;
    double minimum, maximum;
};
constexpr std::array effect_parameters{
    EffectParameter{"low_pass_hz", &AudioBusEffects::low_pass_hz, 20, 20000},
    EffectParameter{"high_pass_hz", &AudioBusEffects::high_pass_hz, 20, 20000},
    EffectParameter{"echo_seconds", &AudioBusEffects::echo_seconds, .01f, 1},
    EffectParameter{"echo_feedback", &AudioBusEffects::echo_feedback, 0, .75},
    EffectParameter{"reverb_decay", &AudioBusEffects::reverb_decay, .1, 10},
    EffectParameter{"reverb_damping", &AudioBusEffects::reverb_damping, 0, .95},
    EffectParameter{"reverb_wet", &AudioBusEffects::reverb_wet, 0, 1}};
void effect_number(double value, const EffectParameter& parameter) {
    require(std::isfinite(value) && value >= parameter.minimum && value <= parameter.maximum,
            std::string("invalid bus effect parameter ") + parameter.name);
}
} // namespace
void validate_audio_bus_effects(const AudioBusEffects& value) {
    for (const auto& parameter : effect_parameters)
        effect_number(value.*parameter.member, parameter);
}
AudioBusEffects audio_bus_effects_from_json(const nlohmann::json& value) {
    require(value.is_object(), "bus effects must be an object");
    AudioBusEffects result;
    for (const auto& [key, item] : value.items()) {
        const auto flag = std::ranges::find_if(
            effect_flags, [&](const auto& entry) { return key == entry.first; });
        if (flag != effect_flags.end()) {
            require(item.is_boolean(), "bus effect switches must be boolean");
            result.*flag->second = item.get<bool>();
            continue;
        }
        const auto parameter = std::ranges::find_if(
            effect_parameters, [&](const auto& entry) { return key == entry.name; });
        require(parameter != effect_parameters.end(), "unsupported bus effect field " + key);
        require(item.is_number(), "bus effect parameters must be numeric");
        const auto number = item.get<double>();
        effect_number(number, *parameter);
        result.*parameter->member = float(number);
    }
    validate_audio_bus_effects(result);
    return result;
}
nlohmann::json audio_bus_effects_json(const AudioBusEffects& value) {
    validate_audio_bus_effects(value);
    auto result = nlohmann::json::object();
    for (const auto& [name, member] : effect_flags)
        result[name] = value.*member;
    for (const auto& parameter : effect_parameters)
        result[parameter.name] = value.*parameter.member;
    return result;
}
bool valid_audio_bus_key(std::string_view key) noexcept {
    if (key.empty() || key.size() > audio_bus_key_limit || key.front() < 'a' || key.front() > 'z' ||
        terminal(key))
        return false;
    return std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}
void validate_audio_bus_volumes(const AudioBusVolumes& values) {
    require(values.size() <= audio_custom_bus_limit, "at most 16 custom bus volumes are supported");
    for (const auto& [key, value] : values) {
        require(valid_audio_bus_key(key), "invalid custom bus key " + key);
        volume(value);
    }
}
AudioRoutingProjection AudioRoutingProjection::compile(const AudioRouting& routing,
                                                       const AudioBusVolumes& overrides,
                                                       bool require_complete_volumes) {
    require(routing.buses.size() <= audio_custom_bus_limit,
            "at most 16 custom buses are supported");
    validate_audio_bus_volumes(overrides);
    std::map<std::string, std::size_t, std::less<>> indices;
    AudioRoutingProjection result;
    for (std::size_t i = 0; i < routing.buses.size(); ++i) {
        const auto& bus = routing.buses[i];
        require(valid_audio_bus_key(bus.key), "invalid or reserved custom bus key " + bus.key);
        require(indices.emplace(bus.key, i).second, "duplicate custom bus key " + bus.key);
        require(bus.label.size() <= audio_bus_label_limit &&
                    std::none_of(bus.label.begin(), bus.label.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "bus labels need at most 128 UTF-8 bytes without control characters");
        (void)nlohmann::json(bus.label).dump();
        volume(bus.volume);
        validate_audio_bus_effects(bus.effects);
        require(bus.sends.size() <= audio_bus_send_limit,
                "a bus supports one output and at most three sends");
        const auto target_key = [](std::string_view key) {
            return terminal(key) || valid_audio_bus_key(key);
        };
        require(target_key(bus.output), "invalid output target key");
        for (const auto& send : bus.sends) {
            require(target_key(send.target), "invalid send target key");
            volume(send.gain);
        }
        result.volumes_.emplace(bus.key, bus.volume);
    }
    if (require_complete_volumes)
        require(overrides.size() == indices.size(), "live volumes must match all custom bus keys");
    for (const auto& [key, value] : overrides) {
        const auto found = result.volumes_.find(key);
        require(found != result.volumes_.end(), "unknown custom bus volume " + key);
        found->second = value;
    }
    using Weights = std::array<long double, 4>;
    std::array<Weights, audio_custom_bus_limit> actual{}, potential{};
    std::array<unsigned char, audio_custom_bus_limit> state{};
    const auto visit = [&](auto&& self, std::size_t i) -> void {
        require(state[i] != 1, "custom bus graph contains a cycle at " + routing.buses[i].key);
        if (state[i] == 2)
            return;
        state[i] = 1;
        const auto& bus = routing.buses[i];
        auto edges = bus.sends;
        edges.push_back({bus.output, 1});
        std::sort(edges.begin(), edges.end(),
                  [](const auto& a, const auto& b) { return a.target < b.target; });
        std::string previous;
        for (const auto& edge : edges) {
            require(!edge.target.empty() && edge.target != previous,
                    "outputs and sends need distinct nonempty targets");
            previous = edge.target;
            volume(edge.gain);
            const auto target = terminal(edge.target);
            if (target) {
                actual[i][*target] += edge.gain;
                potential[i][*target] += edge.gain;
            } else {
                const auto found = indices.find(edge.target);
                require(found != indices.end(), "unknown target bus " + edge.target);
                self(self, found->second);
                for (unsigned axis = 0; axis < 4; ++axis) {
                    actual[i][axis] += edge.gain * actual[found->second][axis];
                    potential[i][axis] += edge.gain * potential[found->second][axis];
                }
            }
        }
        require(std::accumulate(potential[i].begin(), potential[i].end(), 0.L) <=
                    audio_routing_gain_limit,
                "potential path gain exceeds 16 at bus " + bus.key);
        for (auto& value : actual[i])
            value *= result.volumes_.at(bus.key);
        state[i] = 2;
    };
    for (const auto& [key, index] : indices) {
        visit(visit, index);
        AudioBusWeights bus{key, {}};
        std::transform(actual[index].begin(), actual[index].end(), bus.terminals.begin(),
                       [](long double value) { return double(value); });
        result.buses_.push_back(std::move(bus));
    }
    return result;
}
float AudioRoutingProjection::gain(std::string_view custom_bus, std::uint32_t stock_bus,
                                   const std::array<float, 3>& channels) const {
    require(stock_bus < 4, "stock bus must be Master, Music, Effects or Dialogue");
    for (float value : channels)
        volume(value);
    if (custom_bus.empty())
        return stock_bus == 0 ? 1 : channels[stock_bus - 1];
    const auto found =
        std::lower_bound(buses_.begin(), buses_.end(), custom_bus,
                         [](const auto& bus, std::string_view key) { return bus.key < key; });
    if (found == buses_.end() || found->key != custom_bus)
        require(false, "custom bus is not defined: " + std::string(custom_bus));
    long double value = found->terminals[0];
    for (unsigned i = 0; i < 3; ++i)
        value += found->terminals[i + 1] * channels[i];
    return float(value);
}
void validate_audio_routing(const AudioRouting& routing) {
    (void)AudioRoutingProjection::compile(routing);
}
AudioRouting audio_routing_from_json(const nlohmann::json& value) {
    fields(value, {"version", "buses"});
    require(value.at("version").is_number_integer() && value.at("version") == 1,
            "unsupported routing version");
    const auto& buses = value.at("buses");
    require(buses.is_array() && buses.size() <= audio_custom_bus_limit,
            "buses must be an array with at most 16 entries");
    AudioRouting result;
    for (const auto& entry : buses) {
        fields(entry, {"key", "label", "volume", "output", "sends", "effects"});
        AudioBusDefinition bus;
        bus.key = text(entry.at("key"), audio_bus_key_limit);
        bus.output = text(entry.at("output"), audio_bus_key_limit);
        if (entry.contains("label"))
            bus.label = text(entry.at("label"), audio_bus_label_limit);
        if (entry.contains("volume"))
            bus.volume = read_volume(entry.at("volume"));
        if (entry.contains("effects"))
            bus.effects = audio_bus_effects_from_json(entry.at("effects"));
        if (entry.contains("sends")) {
            const auto& sends = entry.at("sends");
            require(sends.is_array() && sends.size() <= audio_bus_send_limit,
                    "at most three sends are supported");
            for (const auto& send : sends) {
                fields(send, {"target", "gain"});
                bus.sends.push_back(
                    {text(send.at("target"), audio_bus_key_limit), read_volume(send.at("gain"))});
            }
        }
        result.buses.push_back(std::move(bus));
    }
    validate_audio_routing(result);
    return result;
}
nlohmann::json audio_routing_json(const AudioRouting& value) {
    validate_audio_routing(value);
    auto buses = nlohmann::json::array();
    for (const auto& bus : value.buses) {
        auto sends = nlohmann::json::array();
        for (const auto& send : bus.sends)
            sends.push_back({{"target", send.target}, {"gain", send.gain}});
        buses.push_back({{"key", bus.key},
                         {"label", bus.label},
                         {"volume", bus.volume},
                         {"output", bus.output},
                         {"sends", std::move(sends)}});
        if (bus.effects != AudioBusEffects{})
            buses.back()["effects"] = audio_bus_effects_json(bus.effects);
    }
    return {{"version", 1}, {"buses", std::move(buses)}};
}
AudioBusVolumes audio_bus_volumes_from_json(const nlohmann::json& value) {
    require(value.is_object() && value.size() <= audio_custom_bus_limit,
            "custom bus volumes require an object with at most 16 keys");
    AudioBusVolumes result;
    for (const auto& [key, item] : value.items()) {
        require(valid_audio_bus_key(key), "invalid custom bus key " + key);
        result.emplace(key, read_volume(item));
    }
    validate_audio_bus_volumes(result);
    return result;
}
} // namespace forge
