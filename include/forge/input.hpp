#pragma once
#include <cstddef>
#include <cstdint>
#include <forge/identity.hpp>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace forge {
struct ActionIdTag;
using ActionId = PersistentId<ActionIdTag>;
enum class ActionKind { Digital, Axis1, Axis2 };
inline constexpr std::size_t touch_contact_limit = 8;
// Logical local-controller slots use 1..4; unindexed pad.* keeps active-pad input.
inline constexpr std::size_t gamepad_slot_limit = 4;
inline constexpr std::size_t gamepad_device_limit = 16;
struct InputControl {
    std::string id;
    bool relative = false;
    bool digital = false;
    bool unipolar = false; // Continuous values in [0,1], e.g. triggers/touch.
};
const std::vector<InputControl>& input_controls();
const InputControl& input_control(const std::string& id);
// Requires a registered control; the paired axis keeps its logical slot prefix.
bool is_gamepad_stick_control(const std::string& id);
// Known controls: null for other devices, 0 for active pad, 1..4 for logical slots.
std::optional<std::uint32_t> gamepad_control_slot(const std::string& id);
std::string gamepad_control_id(std::uint32_t slot, std::string_view leaf);
struct InputBinding {
    std::string control;
    double x = 1, y = 0, deadzone = 0;
    bool radial = false;
    double threshold = .5;
    int direction = 1;
    double outer_deadzone = 0, response_exponent = 1;
};
struct InputVector {
    double x = 0, y = 0;
};
// Inner/outer deadzones followed by a sign-preserving power response. Radial
// conditioning acts on magnitude, retaining the stick direction.
double input_axis(double value, double deadzone, double outer_deadzone = 0,
                  double response_exponent = 1);
InputVector input_stick(double x, double y, double deadzone, double outer_deadzone = 0,
                        double response_exponent = 1);
struct InputAction {
    ActionId id;
    std::string name;
    ActionKind kind;
    std::vector<InputBinding> bindings;
    std::string context;
};
struct InputContext {
    std::string name;
    int priority = 0;
    bool consume = true;
    bool active = false;
    bool control = false; // Owner control frame; never a simulation tick.
};
struct InputBindingConflict {
    ActionId action;
    std::string context, control;
    bool same_context = false;
};
class InputMap {
  public:
    explicit InputMap(nlohmann::json source = {{"version", 1},
                                               {"actions", nlohmann::json::array()}});
    const std::vector<InputAction>& actions() const { return actions_; }
    const nlohmann::json& source() const { return source_; }
    const std::vector<InputContext>& contexts() const { return contexts_; }
    // Produces a validated candidate; the current map never changes on failure.
    InputMap with_bindings(ActionId action, const nlohmann::json& bindings) const;
    std::vector<InputBindingConflict> binding_conflicts(ActionId action,
                                                        const std::string& control) const;

  private:
    nlohmann::json source_;
    std::vector<InputAction> actions_;
    std::vector<InputContext> contexts_;
};
struct InputEvent {
    std::string control;
    double value = 0;
    bool reset = false;
};
// Transient gamepad output. Durations are bounded real milliseconds, not ticks.
struct GamepadRumble {
    double low = 0, high = 0;
    std::uint32_t duration_ms = 0;
    // Nonzero targets a logical slot. Zero retains active-pad output; a zero-
    // strength untargeted request stops all owned feedback.
    std::uint32_t slot = 0;
    void validate() const;
};
// Copied host observations/commands, not saved player/device identities. Tokens
// name one open connection in its host; concrete SDL handles remain private.
struct GamepadDeviceInfo {
    std::string device, name;
    std::uint32_t slot = 0; // Unassigned, or 1..gamepad_slot_limit.
    bool connected = false, rumble = false;
};
struct GamepadTopology {
    bool available = false;
    std::uint64_t revision = 0;
    std::string active;
    std::vector<GamepadDeviceInfo> devices;
    void validate() const;
};
struct GamepadAssignment {
    std::uint64_t revision = 0;
    std::string device;
    std::uint32_t slot = 0; // Zero unassigns this device.
    void validate() const;
};
void to_json(nlohmann::json&, const GamepadTopology&);
void from_json(const nlohmann::json&, GamepadTopology&);
void to_json(nlohmann::json&, const GamepadAssignment&);
void from_json(const nlohmann::json&, GamepadAssignment&);
void to_json(nlohmann::json&, const GamepadRumble&);
void from_json(const nlohmann::json&, GamepadRumble&);
void validate_input_event(const InputEvent& event);
void to_json(nlohmann::json& j, const InputEvent& event);
void from_json(const nlohmann::json& j, InputEvent& event);
struct ActionState {
    double x = 0, y = 0;
    bool held = false, pressed = false, released = false;
};
struct InputSnapshot {
    std::uint64_t tick = 0;
    std::map<ActionId, ActionState> actions;
};
// Single runtime-owner thread. A batch validates completely before altering state.
// Readers borrow the const snapshot only during its fixed tick.
class RuntimeInput {
  public:
    void configure(InputMap map);
    void replace_map(InputMap map); // Settings boundary; preserve active context names.
    void submit(const std::vector<InputEvent>& events);
    void release_all();
    const InputSnapshot& latch(std::uint64_t tick);
    const InputSnapshot& latch_controls(std::uint64_t frame);
    const InputSnapshot& snapshot() const { return current_; }
    const InputMap& map() const { return map_; }
    // Replace the active stack atomically. Higher priority wins; equal priority
    // uses declaration order. A change neutralizes pending input.
    void activate_contexts(const std::vector<std::string>& names);
    bool context_active(const std::string& name) const;
    // Listening consumes incoming controls until completion/cancellation. The
    // caller decides whether to commit a validated binding candidate.
    // Gamepad aliases are filtered to this logical slot; 0 keeps active-pad
    // capture. Keyboard/mouse/touch remain eligible as before.
    void begin_rebind(std::uint32_t gamepad_slot = 0);
    std::uint32_t rebind_gamepad_slot() const { return rebind_gamepad_slot_; }
    void cancel_rebind();
    bool rebinding() const { return listening_; }
    std::optional<InputEvent> take_rebind();

  private:
    ActionState evaluate(const InputAction& action, bool relative) const;
    void event(const InputEvent& event);
    void rebuild_routes();
    bool control_action(const InputAction&) const;
    const InputSnapshot& latch_domain(std::uint64_t sequence, bool control);
    InputMap map_;
    std::map<std::string, double> controls_;
    std::map<ActionId, ActionState> pending_;
    InputSnapshot current_;
    InputSnapshot control_current_;
    std::map<std::string, double> control_deltas_;
    std::set<std::string> active_contexts_;
    std::map<ActionId, std::set<std::string>> routes_;
    bool listening_ = false;
    std::uint32_t rebind_gamepad_slot_ = 0;
    std::set<std::string> rebind_held_;
    std::optional<InputEvent> rebind_result_;
};
// A bounded built-in fixed-pipeline consumer, independent of game action names.
class InputMonitor {
  public:
    void consume(const InputSnapshot& snapshot);
    nlohmann::json status(const InputMap& map) const;
    void reset() {
        counts_.clear();
        tick_ = 0;
    }

  private:
    struct Counts {
        ActionState state;
        std::uint64_t presses = 0, releases = 0, pressed_tick = 0, released_tick = 0;
    };
    std::map<ActionId, Counts> counts_;
    std::uint64_t tick_ = 0;
};
} // namespace forge
