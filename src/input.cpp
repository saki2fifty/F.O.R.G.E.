#include <algorithm>
#include <cmath>
#include <forge/input.hpp>
#include <set>
namespace forge {
using Json = nlohmann::json;
const std::vector<InputControl>& input_controls() {
    static const auto controls = [] {
        std::vector<InputControl> result;
        for (char key = 'a'; key <= 'z'; ++key)
            result.push_back({std::string("key.") + key, false, true});
        for (char key = '0'; key <= '9'; ++key)
            result.push_back({std::string("key.") + key, false, true});
        for (const auto* key : {"space", "left", "right", "up", "down", "lshift", "rshift", "lctrl",
                                "rctrl", "lalt", "ralt", "tab", "enter", "backspace"})
            result.push_back({std::string("key.") + key, false, true});
        for (const auto* button : {"left", "right", "middle", "x1", "x2"})
            result.push_back({std::string("mouse.") + button, false, true});
        for (const auto* axis : {"delta_x", "delta_y", "wheel_x", "wheel_y"})
            result.push_back({std::string("mouse.") + axis, true, false});
        for (const auto* button :
             {"south", "east", "west", "north", "back", "guide", "start", "left_stick",
              "right_stick", "left_shoulder", "right_shoulder", "up", "down", "left", "right"})
            result.push_back({std::string("pad.") + button, false, true});
        for (const auto* axis :
             {"left_x", "left_y", "right_x", "right_y", "left_trigger", "right_trigger"})
            result.push_back({std::string("pad.") + axis, false, false});
        return result;
    }();
    return controls;
}
const InputControl& input_control(const std::string& id) {
    static const auto lookup = [] {
        std::map<std::string, const InputControl*> result;
        for (const auto& control : input_controls())
            result.emplace(control.id, &control);
        return result;
    }();
    const auto found = lookup.find(id);
    if (found != lookup.end())
        return *found->second;
    throw std::runtime_error("Unsupported input control: " + id);
}
InputMap::InputMap(Json source) : source_(std::move(source)) {
    if (!source_.is_object() || source_.at("version") != 1 ||
        !source_.at("version").is_number_integer() || !source_.at("actions").is_array() ||
        source_.at("actions").size() > 64)
        throw std::runtime_error("Input map requires version 1 and at most 64 actions");
    std::set<ActionId> ids;
    for (const auto& a : source_.at("actions")) {
        InputAction action;
        action.id = a.at("id").get<ActionId>();
        action.name = a.at("name").get<std::string>();
        if (!ids.insert(action.id).second || action.name.empty() || action.name.size() > 128)
            throw std::runtime_error("Invalid/duplicate input action identity or name");
        const auto kind = a.at("kind").get<std::string>();
        if (kind == "digital")
            action.kind = ActionKind::Digital;
        else if (kind == "axis1")
            action.kind = ActionKind::Axis1;
        else if (kind == "axis2")
            action.kind = ActionKind::Axis2;
        else
            throw std::runtime_error("Unknown input action kind");
        if (!a.at("bindings").is_array() || a.at("bindings").size() > 16)
            throw std::runtime_error("An action supports at most 16 bindings");
        for (const auto& b : a.at("bindings")) {
            InputBinding binding{b.at("control").get<std::string>(), b.value("x", 1.0),
                                 b.value("y", 0.0), b.value("deadzone", 0.0)};
            const auto& control = input_control(binding.control);
            if (!std::isfinite(binding.x) || !std::isfinite(binding.y) ||
                std::abs(binding.x) > 100 || std::abs(binding.y) > 100 ||
                !std::isfinite(binding.deadzone) || binding.deadzone < 0 || binding.deadzone >= 1 ||
                (action.kind != ActionKind::Axis2 && binding.y != 0) ||
                (action.kind == ActionKind::Digital &&
                 (!control.digital || binding.x != 1 || binding.y != 0)) ||
                ((control.relative || control.digital) && binding.deadzone != 0))
                throw std::runtime_error("Invalid input binding scale/deadzone or digital control");
            action.bindings.push_back(std::move(binding));
        }
        actions_.push_back(std::move(action));
    }
}
void to_json(Json& j, const InputEvent& e) {
    j = e.reset ? Json{{"reset", true}} : Json{{"control", e.control}, {"value", e.value}};
}
void from_json(const Json& j, InputEvent& e) {
    e.reset = j.value("reset", false);
    if (e.reset) {
        e.control.clear();
        e.value = 0;
        return;
    }
    e.control = j.at("control").get<std::string>();
    e.value = j.at("value").get<double>();
}
void RuntimeInput::configure(InputMap map) {
    map_ = std::move(map);
    controls_.clear();
    pending_.clear();
    current_ = {};
}
ActionState RuntimeInput::evaluate(const InputAction& action, bool relative) const {
    ActionState state;
    for (const auto& b : action.bindings) {
        const auto& c = input_control(b.control);
        if (c.relative != relative)
            continue;
        const auto found = controls_.find(b.control);
        double value = found == controls_.end() ? 0 : found->second;
        if (!c.relative && !c.digital)
            value = std::abs(value) <= b.deadzone
                        ? 0
                        : std::copysign((std::abs(value) - b.deadzone) / (1 - b.deadzone), value);
        if (action.kind == ActionKind::Digital)
            state.held |= value != 0;
        else {
            state.x += value * b.x;
            state.y += value * b.y;
        }
    }
    if (action.kind == ActionKind::Digital)
        state.x = state.held ? 1 : 0;
    else if (!relative) {
        state.x = std::clamp(state.x, -1.0, 1.0);
        state.y = std::clamp(state.y, -1.0, 1.0);
        if (action.kind == ActionKind::Axis2) {
            const auto length = std::hypot(state.x, state.y);
            if (length > 1) {
                state.x /= length;
                state.y /= length;
            }
        }
    }
    return state;
}
void RuntimeInput::event(const InputEvent& e) {
    if (e.reset) {
        release_all();
        return;
    }
    if (input_control(e.control).relative)
        controls_[e.control] = std::clamp(controls_[e.control] + e.value, -1000000.0, 1000000.0);
    else
        controls_[e.control] = e.value;
    for (const auto& a : map_.actions())
        if (a.kind == ActionKind::Digital &&
            std::any_of(a.bindings.begin(), a.bindings.end(),
                        [&](const auto& binding) { return binding.control == e.control; })) {
            auto& previous = pending_[a.id];
            const auto next = evaluate(a, false);
            previous.pressed |= next.held && !previous.held;
            previous.released |= !next.held && previous.held;
            previous.held = next.held;
            previous.x = next.x;
        }
}
void RuntimeInput::submit(const std::vector<InputEvent>& events) {
    if (events.size() > 4096)
        throw std::runtime_error("Input batch exceeds 4096 events");
    for (const auto& e : events)
        if (!e.reset) {
            const auto& c = input_control(e.control);
            if (!std::isfinite(e.value) || std::abs(e.value) > (c.relative ? 1000000 : 1) ||
                (c.digital && e.value != 0 && e.value != 1) ||
                (e.control.ends_with("_trigger") && e.value < 0))
                throw std::runtime_error("Invalid input control value");
        }
    for (const auto& e : events)
        event(e);
}
void RuntimeInput::release_all() {
    controls_.clear();
    for (const auto& a : map_.actions()) {
        auto& p = pending_[a.id];
        const auto current = current_.actions.find(a.id);
        const bool was_held = p.held || (current != current_.actions.end() && current->second.held);
        p = {};
        p.released = was_held;
    }
}
const InputSnapshot& RuntimeInput::latch(std::uint64_t tick) {
    current_.tick = tick;
    current_.actions.clear();
    for (const auto& a : map_.actions()) {
        auto state = evaluate(a, false);
        if (a.kind == ActionKind::Digital) {
            auto& p = pending_[a.id];
            state.pressed = p.pressed;
            state.released = p.released;
            p.pressed = p.released = false;
        } else {
            const auto delta = evaluate(a, true);
            state.x += delta.x;
            state.y += delta.y;
        }
        current_.actions.emplace(a.id, state);
    }
    for (auto& [control, value] : controls_)
        if (input_control(control).relative)
            value = 0;
    return current_;
}
void InputMonitor::consume(const InputSnapshot& snapshot) {
    tick_ = snapshot.tick;
    std::erase_if(counts_,
                  [&](const auto& entry) { return !snapshot.actions.contains(entry.first); });
    for (const auto& [id, state] : snapshot.actions) {
        auto& c = counts_[id];
        c.state = state;
        if (state.pressed) {
            ++c.presses;
            c.pressed_tick = tick_;
        }
        if (state.released) {
            ++c.releases;
            c.released_tick = tick_;
        }
    }
}
Json InputMonitor::status(const InputMap& map) const {
    Json actions = Json::array();
    for (const auto& a : map.actions()) {
        const auto it = counts_.find(a.id);
        const auto c = it == counts_.end() ? Counts{} : it->second;
        actions.push_back({{"id", a.id},
                           {"name", a.name},
                           {"x", c.state.x},
                           {"y", c.state.y},
                           {"held", c.state.held},
                           {"pressed", c.state.pressed},
                           {"released", c.state.released},
                           {"presses", c.presses},
                           {"releases", c.releases},
                           {"pressed_tick", c.pressed_tick},
                           {"released_tick", c.released_tick}});
    }
    return {{"tick", tick_}, {"actions", actions}};
}
} // namespace forge
