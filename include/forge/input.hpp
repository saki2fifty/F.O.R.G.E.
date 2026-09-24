#pragma once
#include <forge/identity.hpp>
#include <map>
#include <optional>
#include <set>
#include <vector>
namespace forge {
struct ActionIdTag;
using ActionId = PersistentId<ActionIdTag>;
enum class ActionKind { Digital, Axis1, Axis2 };
struct InputControl {
    std::string id;
    bool relative = false;
    bool digital = false;
};
const std::vector<InputControl>& input_controls();
const InputControl& input_control(const std::string& id);
struct InputBinding {
    std::string control;
    double x = 1, y = 0, deadzone = 0;
    bool radial = false;
    double threshold = .5;
    int direction = 1;
};
struct InputVector {
    double x = 0, y = 0;
};
// Radial dead zone with continuous remapping to the unit disk.
InputVector input_stick(double x, double y, double deadzone);
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
    void begin_rebind();
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
