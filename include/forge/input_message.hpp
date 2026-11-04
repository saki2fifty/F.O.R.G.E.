#pragma once
#include <cstdint>
#include <forge/identity.hpp>
#include <stdexcept>
#include <string>

namespace forge {
inline constexpr unsigned input_message_trigger_profile_version = 1;
enum class InputMessageTrigger : std::uint32_t { Pressed, Released, Changed };
inline constexpr const char* input_message_trigger_name(std::uint32_t trigger) {
    switch (InputMessageTrigger(trigger)) {
    case InputMessageTrigger::Pressed:
        return "Pressed";
    case InputMessageTrigger::Released:
        return "Released";
    case InputMessageTrigger::Changed:
        return "Changed";
    }
    return nullptr;
}
// Action and component keys are durable authoring values. Native type IDs and
// event storage belong exclusively to the current runtime World.
struct InputMessage {
    bool enabled = true;
    std::string action, payload;
    std::uint32_t trigger = std::uint32_t(InputMessageTrigger::Pressed);
    void validate() const {
        if (!input_message_trigger_name(trigger))
            throw std::runtime_error("Input Message trigger must be Pressed, Released or Changed");
        if (!action.empty())
            (void)detail::parse_uuid(action);
        if (payload.size() > 128)
            throw std::runtime_error("Input Message payload key exceeds 128 bytes");
        for (const unsigned char c : payload)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-'))
                throw std::runtime_error("Input Message requires a registered component key");
    }
    bool operator==(const InputMessage&) const = default;
};
} // namespace forge
