#pragma once
#include <forge/identity.hpp>
#include <stdexcept>
#include <string>

namespace forge {
// Action and component keys are durable authoring values. Native type IDs and
// event storage belong exclusively to the current runtime World.
struct InputMessage {
    bool enabled = true;
    std::string action, payload;
    void validate() const {
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
