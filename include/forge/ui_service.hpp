#pragma once
#include <forge/identity.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace forge {
struct UiAction {
    EntityId entity;
    std::string command;
    nlohmann::json value;
};
// Internal exact-build service. Copies only; no presenter or RmlUi types.
class UiService {
  public:
    virtual ~UiService() = default;
    virtual void publish(EntityId, const std::string& name, const nlohmann::json& value) = 0;
    virtual void allow_action(const std::string& name) = 0;
    virtual std::optional<UiAction>
    poll_action(const std::string&) = 0; // Consume from a fixed simulation system.
};
} // namespace forge
