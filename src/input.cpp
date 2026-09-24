#include <algorithm>
#include <cmath>
#include <forge/input.hpp>
#include <set>
namespace forge {
using Json = nlohmann::json;
namespace {
std::string radial_partner(const InputBinding& binding) {
    auto partner = binding.control;
    partner.back() = partner.back() == 'x' ? 'y' : 'x';
    return partner;
}
bool binding_reads(const InputBinding& binding, const std::string& control) {
    return binding.control == control || (binding.radial && radial_partner(binding) == control);
}
} // namespace

InputVector input_stick(double x, double y, double deadzone) {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1 ||
        !std::isfinite(deadzone) || deadzone < 0 || deadzone >= 1)
        throw std::runtime_error("Invalid stick sample or dead zone");
    const auto length = std::hypot(x, y);
    if (length <= deadzone || length == 0)
        return {};
    const auto scale = (std::min(length, 1.0) - deadzone) / ((1 - deadzone) * length);
    return {x * scale, y * scale};
}
const std::vector<InputControl>& input_controls() {
    static const auto controls = [] {
        std::vector<InputControl> result;
        for (char key = 'a'; key <= 'z'; ++key)
            result.push_back({std::string("key.") + key, false, true});
        for (char key = '0'; key <= '9'; ++key)
            result.push_back({std::string("key.") + key, false, true});
        for (const auto* key : {"space", "left", "right", "up", "down", "lshift", "rshift", "lctrl",
                                "rctrl", "lalt", "ralt", "tab", "enter", "backspace", "escape"})
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
    if (!source_.is_object() || (source_.at("version") != 1 && source_.at("version") != 2) ||
        !source_.at("version").is_number_integer() || !source_.at("actions").is_array() ||
        source_.at("actions").size() > 64)
        throw std::runtime_error("Input map requires version 1 or 2 and at most 64 actions");
    std::set<std::string> contexts;
    if (source_.at("version") == 2) {
        const auto& declarations = source_.at("contexts");
        if (!declarations.is_array() || declarations.empty() || declarations.size() > 32)
            throw std::runtime_error("Input map requires 1 to 32 contexts");
        for (const auto& item : declarations) {
            InputContext context;
            context.name = item.at("name").get<std::string>();
            if (context.name.empty() || context.name.size() > 128 ||
                context.name.find('\0') != std::string::npos ||
                !contexts.insert(context.name).second)
                throw std::runtime_error("Invalid/duplicate input context name");
            const auto priority = item.value("priority", Json(0));
            if (!priority.is_number_integer() || priority.get<double>() < -10000 ||
                priority.get<double>() > 10000)
                throw std::runtime_error("Invalid input context priority");
            context.priority = priority.get<int>();
            context.consume = item.value("consume", true);
            context.active = item.value("active", false);
            const auto phase = item.value("phase", std::string("fixed"));
            if (phase != "fixed" && phase != "control")
                throw std::runtime_error("Input context phase must be fixed or control");
            context.control = phase == "control";
            contexts_.push_back(std::move(context));
        }
    } else if (source_.contains("contexts")) {
        throw std::runtime_error("Input contexts require input map version 2");
    }
    std::set<ActionId> ids;
    for (const auto& a : source_.at("actions")) {
        InputAction action;
        action.id = a.at("id").get<ActionId>();
        action.name = a.at("name").get<std::string>();
        if (!contexts_.empty()) {
            action.context = a.at("context").get<std::string>();
            if (!contexts.contains(action.context))
                throw std::runtime_error("Input action references an unknown context");
        } else if (a.contains("context")) {
            throw std::runtime_error("Input action context requires input map version 2");
        }
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
                                 b.value("y", 0.0), b.value("deadzone", 0.0),
                                 b.value("radial", false)};
            const auto& control = input_control(binding.control);
            binding.threshold = b.value("threshold", .5);
            const auto direction = b.value("direction", Json(1));
            if (!direction.is_number_integer() || (direction != 1 && direction != -1))
                throw std::runtime_error("Input binding direction must be +1 or -1");
            binding.direction = direction.get<int>();
            if (!std::isfinite(binding.x) || !std::isfinite(binding.y) ||
                std::abs(binding.x) > 100 || std::abs(binding.y) > 100 ||
                !std::isfinite(binding.deadzone) || binding.deadzone < 0 || binding.deadzone >= 1 ||
                (action.kind != ActionKind::Axis2 && binding.y != 0) ||
                (action.kind == ActionKind::Digital &&
                 (binding.x != 1 || binding.y != 0 ||
                  binding.control.starts_with("mouse.delta_"))) ||
                !std::isfinite(binding.threshold) || binding.threshold <= 0 ||
                binding.threshold > 1 ||
                (control.digital && (binding.direction != 1 || binding.threshold != .5)) ||
                (action.kind != ActionKind::Digital &&
                 (binding.direction != 1 || binding.threshold != .5)) ||
                (binding.control.ends_with("_trigger") && binding.direction != 1) ||
                ((control.relative || control.digital) && binding.deadzone != 0))
                throw std::runtime_error("Invalid input binding scale/deadzone or digital control");
            if (binding.radial && binding.control != "pad.left_x" &&
                binding.control != "pad.left_y" && binding.control != "pad.right_x" &&
                binding.control != "pad.right_y")
                throw std::runtime_error("Radial dead zone requires a gamepad stick axis");
            if (source_.at("version") == 2 &&
                std::any_of(action.bindings.begin(), action.bindings.end(),
                            [&](const auto& old) { return old.control == binding.control; }))
                throw std::runtime_error("Duplicate control on the same input action");
            action.bindings.push_back(std::move(binding));
        }
        actions_.push_back(std::move(action));
    }
}
InputMap InputMap::with_bindings(ActionId id, const Json& bindings) const {
    std::set<std::string> controls;
    for (const auto& binding : bindings)
        if (!controls.insert(binding.at("control").get<std::string>()).second)
            throw std::runtime_error("Duplicate control on the same input action");
    auto candidate = source_;
    for (auto& action : candidate["actions"])
        if (action.at("id").get<ActionId>() == id) {
            action["bindings"] = bindings;
            return InputMap(std::move(candidate));
        }
    throw std::runtime_error("Binding targets an unknown ActionId");
}
std::vector<InputBindingConflict> InputMap::binding_conflicts(ActionId id,
                                                              const std::string& control) const {
    (void)input_control(control);
    const auto target = std::find_if(actions_.begin(), actions_.end(),
                                     [&](const auto& action) { return action.id == id; });
    if (target == actions_.end())
        throw std::runtime_error("Binding targets an unknown ActionId");
    std::vector<InputBindingConflict> result;
    for (const auto& action : actions_)
        for (const auto& binding : action.bindings)
            if (binding_reads(binding, control))
                result.push_back(
                    {action.id, action.context, control, action.context == target->context});
    return result;
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
    control_current_ = {};
    control_deltas_.clear();
    listening_ = false;
    rebind_held_.clear();
    rebind_result_.reset();
    active_contexts_.clear();
    for (const auto& context : map_.contexts())
        if (context.active)
            active_contexts_.insert(context.name);
    rebuild_routes();
}
void RuntimeInput::replace_map(InputMap map) {
    std::vector<std::string> active;
    for (const auto& context : map.contexts())
        if (active_contexts_.contains(context.name))
            active.push_back(context.name);
    configure(std::move(map));
    activate_contexts(active);
}
bool RuntimeInput::control_action(const InputAction& action) const {
    for (const auto& context : map_.contexts())
        if (context.name == action.context)
            return context.control;
    return false;
}
void RuntimeInput::begin_rebind() {
    rebind_held_.clear();
    for (const auto& [control, value] : controls_)
        if (!input_control(control).relative && std::abs(value) > .25)
            rebind_held_.insert(control);
    release_all();
    rebind_result_.reset();
    listening_ = true;
}
void RuntimeInput::cancel_rebind() {
    listening_ = false;
    rebind_held_.clear();
    rebind_result_.reset();
    release_all();
}
std::optional<InputEvent> RuntimeInput::take_rebind() {
    auto result = std::move(rebind_result_);
    rebind_result_.reset();
    return result;
}
bool RuntimeInput::context_active(const std::string& name) const {
    return active_contexts_.contains(name);
}
void RuntimeInput::activate_contexts(const std::vector<std::string>& names) {
    std::set<std::string> candidate;
    for (const auto& name : names) {
        if (!candidate.insert(name).second ||
            std::none_of(map_.contexts().begin(), map_.contexts().end(),
                         [&](const auto& context) { return context.name == name; }))
            throw std::runtime_error("Invalid/duplicate active input context: " + name);
    }
    if (candidate == active_contexts_)
        return;
    release_all();
    active_contexts_ = std::move(candidate);
    rebuild_routes();
}
void RuntimeInput::rebuild_routes() {
    routes_.clear();
    if (map_.contexts().empty()) {
        for (const auto& action : map_.actions())
            for (const auto& binding : action.bindings) {
                routes_[action.id].insert(binding.control);
                if (binding.radial)
                    routes_[action.id].insert(radial_partner(binding));
            }
        return;
    }
    std::vector<const InputContext*> contexts;
    for (const auto& context : map_.contexts())
        if (active_contexts_.contains(context.name))
            contexts.push_back(&context);
    std::stable_sort(contexts.begin(), contexts.end(),
                     [](auto a, auto b) { return a->priority > b->priority; });
    std::set<std::string> consumed;
    for (const auto* context : contexts) {
        std::set<std::string> claims;
        for (const auto& action : map_.actions())
            if (action.context == context->name)
                for (const auto& binding : action.bindings) {
                    if (!consumed.contains(binding.control))
                        routes_[action.id].insert(binding.control);
                    if (context->consume)
                        claims.insert(binding.control);
                    if (binding.radial) {
                        const auto partner = radial_partner(binding);
                        if (!consumed.contains(partner))
                            routes_[action.id].insert(partner);
                        if (context->consume)
                            claims.insert(partner);
                    }
                }
        consumed.insert(claims.begin(), claims.end());
    }
}
ActionState RuntimeInput::evaluate(const InputAction& action, bool relative) const {
    ActionState state;
    const auto route = routes_.find(action.id);
    if (route == routes_.end())
        return state;
    for (const auto& b : action.bindings) {
        if (!route->second.contains(b.control))
            continue;
        const auto& c = input_control(b.control);
        if (c.relative != relative)
            continue;
        const auto& values = relative && control_action(action) ? control_deltas_ : controls_;
        const auto found = values.find(b.control);
        double value = found == values.end() ? 0 : found->second;
        if (b.radial) {
            const auto prefix = b.control.substr(0, b.control.size() - 1);
            auto sample = [&](const std::string& axis) {
                // A higher context may consume the other axis. Never read it
                // through radial processing and bypass that routing decision.
                const auto position = controls_.find(axis);
                return route->second.contains(axis) && position != controls_.end()
                           ? position->second
                           : 0.0;
            };
            const auto stick = input_stick(sample(prefix + "x"), sample(prefix + "y"), b.deadzone);
            value = b.control.back() == 'x' ? stick.x : stick.y;
        } else if (!c.relative && !c.digital)
            value = std::abs(value) <= b.deadzone
                        ? 0
                        : std::copysign((std::abs(value) - b.deadzone) / (1 - b.deadzone), value);
        if (action.kind == ActionKind::Digital)
            state.held |= c.digital ? value != 0 : value * b.direction >= b.threshold;
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
    if (input_control(e.control).relative) {
        controls_[e.control] = std::clamp(controls_[e.control] + e.value, -1000000.0, 1000000.0);
        control_deltas_[e.control] =
            std::clamp(control_deltas_[e.control] + e.value, -1000000.0, 1000000.0);
    } else
        controls_[e.control] = e.value;
    for (const auto& a : map_.actions())
        if (a.kind == ActionKind::Digital &&
            std::any_of(a.bindings.begin(), a.bindings.end(),
                        [&](const auto& binding) { return binding_reads(binding, e.control); })) {
            auto& previous = pending_[a.id];
            const auto next = evaluate(a, false);
            previous.pressed |= next.held && !previous.held;
            previous.released |= !next.held && previous.held;
            previous.held = next.held;
            previous.x = next.x;
            if (input_control(e.control).relative && !previous.held) {
                const auto route = routes_.find(a.id);
                if (route != routes_.end() && route->second.contains(e.control))
                    for (const auto& binding : a.bindings)
                        if (binding.control == e.control) {
                            const auto& values = control_action(a) ? control_deltas_ : controls_;
                            if (values.at(e.control) * binding.direction >= binding.threshold)
                                previous.pressed = previous.released = true;
                        }
            }
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
    if (listening_) {
        for (const auto& e : events) {
            if (e.reset || (e.control == "key.escape" && e.value != 0)) {
                cancel_rebind();
                break;
            }
            if (rebind_held_.contains(e.control)) {
                if (std::abs(e.value) <= .25)
                    rebind_held_.erase(e.control);
                continue;
            }
            // Incidental pointer motion never steals a button/key rebind.
            if (e.control == "mouse.delta_x" || e.control == "mouse.delta_y")
                continue;
            if (std::abs(e.value) >= .5) {
                rebind_result_ = e;
                listening_ = false;
                rebind_held_.clear();
                break;
            }
        }
        return;
    }
    for (const auto& e : events)
        event(e);
}
void RuntimeInput::release_all() {
    controls_.clear();
    control_deltas_.clear();
    for (const auto& a : map_.actions()) {
        auto& p = pending_[a.id];
        const auto& snapshot = control_action(a) ? control_current_ : current_;
        const auto current = snapshot.actions.find(a.id);
        const bool was_held = p.held || (current != snapshot.actions.end() && current->second.held);
        p = {};
        p.released = was_held;
    }
}
const InputSnapshot& RuntimeInput::latch(std::uint64_t tick) { return latch_domain(tick, false); }
const InputSnapshot& RuntimeInput::latch_controls(std::uint64_t frame) {
    return latch_domain(frame, true);
}
const InputSnapshot& RuntimeInput::latch_domain(std::uint64_t sequence, bool control) {
    auto& snapshot = control ? control_current_ : current_;
    snapshot.tick = sequence;
    snapshot.actions.clear();
    for (const auto& a : map_.actions()) {
        if (control_action(a) != control)
            continue;
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
        snapshot.actions.emplace(a.id, state);
    }
    auto& values = control ? control_deltas_ : controls_;
    for (auto& [name, value] : values)
        if (input_control(name).relative)
            value = 0;
    return snapshot;
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
