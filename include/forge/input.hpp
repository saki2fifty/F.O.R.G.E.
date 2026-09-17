#pragma once
#include <forge/identity.hpp>
#include <map>
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
};
struct InputAction {
    ActionId id;
    std::string name;
    ActionKind kind;
    std::vector<InputBinding> bindings;
};
class InputMap {
  public:
    explicit InputMap(nlohmann::json source = {{"version", 1},
                                               {"actions", nlohmann::json::array()}});
    const std::vector<InputAction>& actions() const { return actions_; }
    const nlohmann::json& source() const { return source_; }

  private:
    nlohmann::json source_;
    std::vector<InputAction> actions_;
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
    void submit(const std::vector<InputEvent>& events);
    void release_all();
    const InputSnapshot& latch(std::uint64_t tick);
    const InputSnapshot& snapshot() const { return current_; }
    const InputMap& map() const { return map_; }

  private:
    ActionState evaluate(const InputAction& action, bool relative) const;
    void event(const InputEvent& event);
    InputMap map_;
    std::map<std::string, double> controls_;
    std::map<ActionId, ActionState> pending_;
    InputSnapshot current_;
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
