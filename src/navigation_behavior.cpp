#include "bounded_json.hpp"
#include "state_graph_layout.hpp"
#include <algorithm>
#include <cmath>
#include <forge/navigation_behavior.hpp>
#include <limits>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::uint64_t identifier(const Json& value, std::uint64_t maximum = UINT32_MAX) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Behavior identity must be a nonnegative integer");
    const auto id = value.get<std::uint64_t>();
    require(id <= maximum, "Behavior identity exceeds its supported range");
    return id;
}
bool version_one(const Json& value) { return value.is_number_integer() && value == 1; }
NavigationBehaviorAction action(const Json& value) {
    require(value.is_string(), "Behavior action kind must be a string");
    for (auto kind : {NavigationBehaviorAction::Wait, NavigationBehaviorAction::MoveToPoint,
                      NavigationBehaviorAction::FollowTarget})
        if (value == navigation_behavior_action_name(kind))
            return kind;
    throw std::runtime_error("Unsupported navigation behavior action");
}
NavigationBehaviorCondition condition(const Json& value) {
    require(value.is_string(), "Behavior condition kind must be a string");
    for (auto kind :
         {NavigationBehaviorCondition::Elapsed, NavigationBehaviorCondition::Arrived,
          NavigationBehaviorCondition::TargetNear, NavigationBehaviorCondition::TargetFar,
          NavigationBehaviorCondition::TargetMissing})
        if (value == navigation_behavior_condition_name(kind))
            return kind;
    throw std::runtime_error("Unsupported navigation behavior condition");
}
double bounded_number(const Json& value, double minimum, double maximum) {
    require(value.is_number(), "Behavior value must be numeric");
    const double number = value.get<double>();
    require(std::isfinite(number) && number >= minimum && number <= maximum,
            "Behavior value exceeds its supported range");
    return number;
}
double condition_value(const Json& source, NavigationBehaviorCondition kind) {
    if (kind == NavigationBehaviorCondition::Elapsed)
        return bounded_number(source.at("seconds"), 0, 3600);
    if (kind == NavigationBehaviorCondition::TargetNear ||
        kind == NavigationBehaviorCondition::TargetFar)
        return bounded_number(source.at("distance"), 0, 8192);
    return 0;
}
} // namespace
const char* navigation_behavior_action_name(NavigationBehaviorAction kind) {
    switch (kind) {
    case NavigationBehaviorAction::Wait:
        return "wait";
    case NavigationBehaviorAction::MoveToPoint:
        return "move_to_point";
    case NavigationBehaviorAction::FollowTarget:
        return "follow_target";
    }
    throw std::runtime_error("Invalid navigation behavior action");
}
const char* navigation_behavior_condition_name(NavigationBehaviorCondition kind) {
    switch (kind) {
    case NavigationBehaviorCondition::Elapsed:
        return "elapsed";
    case NavigationBehaviorCondition::Arrived:
        return "arrived";
    case NavigationBehaviorCondition::TargetNear:
        return "target_near";
    case NavigationBehaviorCondition::TargetFar:
        return "target_far";
    case NavigationBehaviorCondition::TargetMissing:
        return "target_missing";
    }
    throw std::runtime_error("Invalid navigation behavior condition");
}
const NavigationBehaviorState& NavigationBehaviorGraph::state(std::uint32_t id) const {
    const auto found = std::find_if(states.begin(), states.end(),
                                    [id](const auto& value) { return value.id == id; });
    require(found != states.end(), "Behavior state is missing or deleted");
    return *found;
}
std::size_t NavigationBehaviorGraph::resident_bytes() const {
    std::size_t bytes = sizeof(*this) + states.capacity() * sizeof(NavigationBehaviorState) +
                        cooked_digest.capacity();
    for (const auto& s : states)
        bytes +=
            s.name.capacity() + s.transitions.capacity() * sizeof(NavigationBehaviorTransition);
    return bytes;
}
NavigationBehaviorSource NavigationBehaviorSource::create(AssetId id) {
    require(bool(id), "Navigation behavior requires an asset identity");
    return {{{"format", "forge.navigation-behavior"},
             {"version", 1},
             {"asset_id", id},
             {"entry", 1},
             {"next_id", 2},
             {"states", Json::array({{{"id", 1},
                                      {"name", "Wait"},
                                      {"action", {{"kind", "wait"}}},
                                      {"transitions", Json::array()}}})}}};
}
NavigationBehaviorSource NavigationBehaviorSource::parse(std::span<const std::byte> bytes) {
    NavigationBehaviorSource source{
        asset_detail::parse_bounded_json(bytes, navigation_behavior_byte_limit, 12000, 12)};
    source.validate();
    return source;
}
NavigationBehaviorSource NavigationBehaviorSource::parse_cooked(std::span<const std::byte> bytes) {
    const auto value =
        asset_detail::parse_bounded_json(bytes, navigation_behavior_cooked_byte_limit, 12008, 13);
    require(value.is_object() && value.at("format") == "forge.navigation-behavior" &&
                version_one(value.at("version")),
            "Unsupported cooked navigation behavior format/version");
    const auto text = value.at("document").dump();
    auto source = parse(std::as_bytes(std::span(text)));
    (void)source.compiled();
    return source;
}
AssetId NavigationBehaviorSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void NavigationBehaviorSource::validate() const {
    const auto& d = document;
    require(d.is_object() && d.at("format") == "forge.navigation-behavior" &&
                version_one(d.at("version")),
            "Unsupported navigation behavior source format/version");
    const auto text = d.dump();
    const auto saved = asset_detail::parse_bounded_json(std::as_bytes(std::span(text)),
                                                        navigation_behavior_byte_limit, 12000, 12);
    require(saved == d, "Behavior contains values that cannot be saved as JSON");
    require(bool(asset()), "Navigation behavior asset identity is missing");
    const auto next = identifier(d.at("next_id"), std::uint64_t(UINT32_MAX) + 1);
    require(next != 0, "Behavior identity sequence cannot be zero");
    const auto entry = identifier(d.at("entry"));
    const auto& rows = d.at("states");
    require(rows.is_array() && rows.size() <= 32, "Behavior supports at most 32 states");
    std::set<std::uint32_t> ids, states;
    std::set<std::string> names;
    const auto allocated = [&](const Json& value) {
        const auto id = identifier(value);
        require(id && id < next && ids.insert(std::uint32_t(id)).second,
                "Behavior state/transition identity is unallocated or duplicated");
        return std::uint32_t(id);
    };
    std::size_t transitions = 0;
    for (const auto& s : rows) {
        states.insert(allocated(s.at("id")));
        const auto name = s.at("name").get<std::string>();
        require(!name.empty() && name.size() <= 64 && names.insert(name).second &&
                    std::none_of(name.begin(), name.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "Behavior names must be unique and contain 1-64 printable bytes");
        const auto& a = s.at("action");
        if (action(a.at("kind")) == NavigationBehaviorAction::MoveToPoint) {
            const auto& point = a.at("point");
            require(point.is_array() && point.size() == 3,
                    "Behavior point requires three coordinates");
            for (const auto& coordinate : point)
                (void)bounded_number(coordinate, -4096, 4096);
        }
        const auto& edges = s.at("transitions");
        require(edges.is_array() && edges.size() <= 8 && (transitions += edges.size()) <= 128,
                "Behavior supports eight transitions per state and 128 total");
        for (const auto& edge : edges) {
            allocated(edge.at("id"));
            const auto& predicate = edge.at("condition");
            (void)condition_value(predicate, condition(predicate.at("kind")));
        }
    }
    require(rows.empty() ? entry == 0 : states.contains(std::uint32_t(entry)),
            "Behavior entry state is missing");
    for (const auto& s : rows)
        for (const auto& edge : s.at("transitions"))
            require(states.contains(std::uint32_t(identifier(edge.at("to")))),
                    "Behavior transition destination is missing");
    if (asset_detail::has_state_graph_layout(d, navigation_behavior_layout_format))
        require(version_one(d.at("editor_layout").at("version")),
                "Unsupported navigation behavior layout version");
    asset_detail::validate_state_graph_layout(d, navigation_behavior_layout_format);
}
NavigationBehaviorGraph NavigationBehaviorSource::compiled() const {
    validate();
    require(!document.at("states").empty(), "Publish at least one behavior state");
    NavigationBehaviorGraph graph{asset(), std::uint32_t(identifier(document.at("entry"))), {}};
    for (const auto& row : document.at("states")) {
        NavigationBehaviorState s;
        s.id = std::uint32_t(identifier(row.at("id")));
        s.name = row.at("name");
        s.action = action(row.at("action").at("kind"));
        if (s.action == NavigationBehaviorAction::MoveToPoint)
            s.point = row.at("action").at("point").get<std::array<double, 3>>();
        for (const auto& edge : row.at("transitions")) {
            const auto& predicate = edge.at("condition");
            const auto kind = condition(predicate.at("kind"));
            s.transitions.push_back({std::uint32_t(identifier(edge.at("id"))),
                                     std::uint32_t(identifier(edge.at("to"))), kind,
                                     condition_value(predicate, kind)});
        }
        graph.states.push_back(std::move(s));
    }
    return graph;
}
std::vector<std::byte> NavigationBehaviorSource::cook() const {
    (void)compiled();
    auto runtime = document;
    if (asset_detail::has_state_graph_layout(runtime, navigation_behavior_layout_format))
        runtime.erase("editor_layout");
    const auto text = Json{{"format", "forge.navigation-behavior"},
                           {"version", 1},
                           {"document", std::move(runtime)}}
                          .dump();
    require(text.size() <= navigation_behavior_cooked_byte_limit,
            "Cooked navigation behavior exceeds its bound");
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
void to_json(nlohmann::json& value, const NavigationBehaviorCursor& cursor) {
    require(std::isfinite(cursor.elapsed) && cursor.elapsed >= 0 && cursor.elapsed <= 3600,
            "Behavior elapsed time is invalid");
    value = {{"state", cursor.state},
             {"last_transition", cursor.last_transition},
             {"elapsed", cursor.elapsed},
             {"transitions", cursor.transitions}};
}
void from_json(const nlohmann::json& value, NavigationBehaviorCursor& cursor) {
    require(value.is_object() && value.size() == 4, "Invalid behavior cursor fields");
    NavigationBehaviorCursor next;
    next.state = std::uint32_t(identifier(value.at("state")));
    next.last_transition = std::uint32_t(identifier(value.at("last_transition")));
    next.elapsed = bounded_number(value.at("elapsed"), 0, 3600);
    next.transitions = identifier(value.at("transitions"), UINT64_MAX);
    cursor = next;
}
void validate_navigation_behavior_cursor(const NavigationBehaviorGraph& graph,
                                         const NavigationBehaviorCursor& cursor) {
    require(std::isfinite(cursor.elapsed) && cursor.elapsed >= 0 && cursor.elapsed <= 3600,
            "Behavior elapsed time is invalid");
    if (!cursor.state) {
        require(!cursor.last_transition && !cursor.transitions && cursor.elapsed == 0,
                "Uninitialized behavior cursor contains progress");
        return;
    }
    (void)graph.state(cursor.state);
    if (!cursor.transitions) {
        require(!cursor.last_transition && cursor.state == graph.entry,
                "Initial behavior cursor does not select its entry");
        return;
    }
    bool found = false;
    for (const auto& state : graph.states)
        for (const auto& edge : state.transitions)
            found |= edge.id == cursor.last_transition && edge.destination == cursor.state;
    require(found, "Behavior cursor transition does not enter its current state");
}
const NavigationBehaviorState& advance_navigation_behavior(const NavigationBehaviorGraph& graph,
                                                           NavigationBehaviorCursor& cursor,
                                                           const NavigationBehaviorInputs& input,
                                                           double dt) {
    require(std::isfinite(dt) && dt > 0 && dt <= 1, "Behavior requires positive fixed delta <= 1");
    validate_navigation_behavior_cursor(graph, cursor);
    require(!input.target_distance ||
                (std::isfinite(*input.target_distance) && *input.target_distance >= 0),
            "Behavior target distance is invalid");
    auto next = cursor;
    if (!next.state) {
        next.state = graph.entry;
    }
    const auto& current = graph.state(next.state);
    next.elapsed = std::min(3600., next.elapsed + dt);
    for (const auto& edge : current.transitions) {
        bool matches = false;
        switch (edge.condition) {
        case NavigationBehaviorCondition::Elapsed:
            matches = next.elapsed >= edge.value;
            break;
        case NavigationBehaviorCondition::Arrived:
            matches = input.arrived;
            break;
        case NavigationBehaviorCondition::TargetNear:
            matches = input.target_distance && *input.target_distance <= edge.value;
            break;
        case NavigationBehaviorCondition::TargetFar:
            matches = input.target_distance && *input.target_distance >= edge.value;
            break;
        case NavigationBehaviorCondition::TargetMissing:
            matches = !input.target_distance;
            break;
        default:
            throw std::runtime_error("Invalid navigation behavior condition");
        }
        if (!matches)
            continue;
        (void)graph.state(edge.destination);
        require(next.transitions != std::numeric_limits<std::uint64_t>::max(),
                "Behavior transition counter exhausted");
        next.state = edge.destination;
        next.elapsed = 0;
        next.last_transition = edge.id;
        ++next.transitions;
        break;
    }
    const auto& result = graph.state(next.state);
    cursor = next;
    return result;
}
} // namespace forge
