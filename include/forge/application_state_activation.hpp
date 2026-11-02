#pragma once
#include <flecs.h>
#include <memory>

namespace forge {
class WorldContext;
struct ApplicationStateGate;
// Detached admission before publishing a live condition edit.
void validate_application_state_gate(WorldContext&, const ApplicationStateGate&);
// Ephemeral owner-preflight view, not a stored activation authority. Construct
// before iterating a subsystem's candidates, use before world mutation, discard.
// Validates condition values/references outside table-locked callbacks. Native
// membership pairs determine results; structural ancestors compose with AND.
// Authoring/validation preview worlds keep their normal activity.
class ApplicationStateActivation {
  public:
    explicit ApplicationStateActivation(WorldContext&);
    ~ApplicationStateActivation();
    ApplicationStateActivation(ApplicationStateActivation&&) noexcept;
    ApplicationStateActivation& operator=(ApplicationStateActivation&&) noexcept;
    ApplicationStateActivation(const ApplicationStateActivation&) = delete;
    ApplicationStateActivation& operator=(const ApplicationStateActivation&) = delete;
    bool active(flecs::entity_t);

  private:
    WorldContext* context_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
