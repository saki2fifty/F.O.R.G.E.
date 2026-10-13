#include "animation_states_layout.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/animation_states.hpp>
#include <set>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::uint32_t number(const nlohmann::json& value, bool nonzero = true) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Animation state ID must be an unsigned integer");
    const auto id = value.get<std::uint64_t>();
    require(id <= UINT32_MAX && (!nonzero || id != 0),
            "Animation state ID outside supported range");
    return std::uint32_t(id);
}
} // namespace
const AnimationState& AnimationStates::state(std::uint32_t id) const {
    if (!id)
        id = entry;
    const auto found =
        std::find_if(states.begin(), states.end(), [id](const auto& s) { return s.id == id; });
    if (found == states.end())
        throw std::runtime_error("Animation state ID is missing or deleted");
    return *found;
}
const AnimationState& AnimationStates::state(std::string_view name) const {
    const auto found = std::find_if(states.begin(), states.end(),
                                    [name](const auto& s) { return s.name == name; });
    if (found == states.end())
        throw std::runtime_error("Animation state name is missing");
    return *found;
}
std::size_t AnimationStates::resident_bytes() const {
    std::size_t bytes = sizeof(*this) + states.capacity() * sizeof(AnimationState);
    for (const auto& s : states) {
        bytes += s.name.capacity() + s.markers.capacity() * sizeof(AnimationMarker);
        for (const auto& marker : s.markers)
            bytes += marker.name.capacity();
    }
    return bytes;
}
AnimationStatesSource AnimationStatesSource::create(AssetId id) {
    require(bool(id), "Animation States needs an asset identity");
    return {{{"format", "forge.animation-states"},
             {"version", 2},
             {"asset_id", id},
             {"skeleton", nullptr},
             {"entry", 0},
             {"next_id", 1},
             {"next_marker_id", 1},
             {"states", nlohmann::json::array()}}};
}
AnimationStatesSource AnimationStatesSource::parse(std::span<const std::byte> bytes) {
    AnimationStatesSource result{
        asset_detail::parse_bounded_json(bytes, animation_states_byte_limit, 10000, 12)};
    result.validate();
    return result;
}
AssetId AnimationStatesSource::asset() const { return document.at("asset_id").get<AssetId>(); }
AnimationStatesSource AnimationStatesSource::parse_cooked(std::span<const std::byte> bytes,
                                                          std::uint32_t& version) {
    auto cooked =
        asset_detail::parse_bounded_json(bytes, animation_states_cooked_byte_limit, 10007, 13);
    require(cooked.at("format") == "forge.animation-states", "Unsupported cooked state format");
    AnimationStatesSource result;
    if (cooked.at("version") == 1) {
        version = 1;
        require(bytes.size() <= animation_states_byte_limit, "Legacy cooked states too large");
        result = parse(bytes); // Preserve the original legacy node/depth limits too.
    } else {
        require(cooked.at("version") == 2, "Unsupported cooked state version");
        version = 2;
        result.document = std::move(cooked.at("document"));
        const auto source = result.document.dump();
        result = parse(std::as_bytes(std::span(source)));
    }
    result.validate();
    return result;
}
void AnimationStatesSource::validate() const {
    const auto& d = document;
    require(d.is_object() && d.at("format") == "forge.animation-states" &&
                (d.at("version") == 1 || d.at("version") == 2),
            "Unsupported Animation States format/version");
    require(bool(asset()), "Animation States asset identity is empty");
    if (!d.at("skeleton").is_null())
        require(bool(d.at("skeleton").get<AssetId>()), "Invalid state skeleton");
    const auto entry = number(d.at("entry"), false);
    const auto& states = d.at("states");
    require(states.is_array() && states.size() <= 32,
            "Animation States supports at most 32 states");
    require(d.at("next_id").is_number_unsigned() ||
                (d.at("next_id").is_number_integer() && d.at("next_id").get<std::int64_t>() > 0),
            "Animation state ID sequence must be an unsigned integer");
    const auto next = d.at("next_id").get<std::uint64_t>();
    require(next && next <= std::uint64_t(UINT32_MAX) + 1,
            "Animation state ID sequence is invalid");
    const auto marker_next_value = d.value("next_marker_id", nlohmann::json(1));
    require(marker_next_value.is_number_unsigned() || (marker_next_value.is_number_integer() &&
                                                       marker_next_value.get<std::int64_t>() > 0),
            "Animation marker ID sequence must be an unsigned integer");
    const auto marker_next = marker_next_value.get<std::uint64_t>();
    require(marker_next && marker_next <= std::uint64_t(UINT32_MAX) + 1,
            "Animation marker ID sequence is invalid");
    std::set<std::uint32_t> marker_ids;
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    for (const auto& s : states) {
        const auto id = number(s.at("id"));
        const auto name = s.at("name").get<std::string>();
        require(id < next && ids.insert(id).second, "Animation state ID is reused or duplicated");
        require(!name.empty() && name.size() <= 64 && names.insert(name).second &&
                    std::none_of(name.begin(), name.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "Animation state names must be unique and contain 1–64 printable bytes");
        require(bool(s.at("clip").get<AssetId>()), "Animation state needs a clip identity");
        require(s.at("loop").is_boolean(), "Animation state loop must be boolean");
        if (s.contains("markers")) {
            require(s.at("markers").is_array() && s.at("markers").size() <= 64,
                    "Animation state supports at most 64 markers");
            for (const auto& marker : s.at("markers")) {
                const auto mid = number(marker.at("id"));
                const auto label = marker.at("name").get<std::string>();
                require(mid < marker_next && marker_ids.insert(mid).second &&
                            marker_ids.size() <= 512,
                        "Animation marker ID is reused/duplicated or set exceeds 512 markers");
                require(!label.empty() && label.size() <= 64 &&
                            std::none_of(label.begin(), label.end(),
                                         [](unsigned char c) { return c < 32 || c == 127; }),
                        "Animation marker name must contain 1–64 printable bytes");
                require(marker.at("phase").is_number() && marker.at("value").is_number(),
                        "Animation marker phase/value must be numeric");
                const double phase = marker.at("phase"), value = marker.at("value");
                require(std::isfinite(phase) && phase >= 0 && phase <= 1 && std::isfinite(value) &&
                            std::abs(value) <= 1e6,
                        "Animation marker phase/value outside supported range");
            }
        }
        for (const auto* field : {"speed", "transition_seconds"}) {
            require(s.at(field).is_number(), "Animation state speed/fade must be numeric");
            const double v = s.at(field).get<double>();
            require(std::isfinite(v) && v >= 0 &&
                        v <= (std::string_view(field) == "speed" ? 4 : 10),
                    "Animation state speed/fade outside supported range");
        }
    }
    require(states.empty() ? entry == 0 : ids.contains(entry), "Animation entry state is missing");
    asset_detail::validate_state_graph_layout(d);
    if (d.at("version") == 2)
        for (const auto& state : states)
            if (state.contains("exit") && !state.at("exit").is_null()) {
                const auto& exit = state.at("exit");
                require(exit.is_object(), "Animation exit transition must be an object or null");
                const auto target = number(exit.at("state"));
                require(ids.contains(target) && target != number(state.at("id")),
                        "Animation exit target must be another state in this set");
                require(exit.at("phase").is_number(), "Animation exit phase must be numeric");
                const double phase = exit.at("phase");
                require(std::isfinite(phase) && phase >= .001 && phase <= 1,
                        "Animation exit phase must be within 0.001 to 1");
            }
}
AnimationStates AnimationStatesSource::compiled() const {
    validate();
    require(!document.at("states").empty() && !document.at("skeleton").is_null(),
            "Animation States needs a skeleton and at least one state before publishing");
    AnimationStates result{
        asset(), document.at("skeleton").get<AssetId>(), number(document.at("entry")), {}};
    for (const auto& s : document.at("states")) {
        result.states.push_back({number(s.at("id")),
                                 s.at("name"),
                                 {s.at("clip").get<AssetId>()},
                                 s.at("loop"),
                                 s.at("speed"),
                                 s.at("transition_seconds")});
        if (s.contains("markers"))
            for (const auto& marker : s.at("markers"))
                result.states.back().markers.push_back({number(marker.at("id")), marker.at("name"),
                                                        marker.at("phase"), marker.at("value")});
        if (document.at("version") == 2 && s.contains("exit") && !s.at("exit").is_null()) {
            result.states.back().exit_state = number(s.at("exit").at("state"));
            result.states.back().exit_phase = s.at("exit").at("phase");
        }
    }
    return result;
}
std::vector<std::byte> AnimationStatesSource::cook() const {
    (void)compiled();
    const auto source = document.dump();
    (void)parse(std::as_bytes(std::span(source)));
    auto runtime = document;
    if (asset_detail::has_state_graph_layout(runtime))
        runtime.erase("editor_layout");
    const auto text =
        nlohmann::json{{"format", "forge.animation-states"}, {"version", 2}, {"document", runtime}}
            .dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
AnimationStates decode_animation_states(std::span<const std::byte> bytes, AssetId asset) {
    std::uint32_t version;
    auto source = AnimationStatesSource::parse_cooked(bytes, version);
    require(source.asset() == asset, "Cooked Animation States identity mismatch");
    return source.compiled();
}
} // namespace forge
