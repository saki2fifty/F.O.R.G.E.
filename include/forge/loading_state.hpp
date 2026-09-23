#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace forge {
// Copied host state, not authored content or a second scene identity.
struct LoadingState {
    std::uint64_t ticket = 0, superseded_ticket = 0;
    std::string state = "idle", stage, error_code, error;
    std::size_t completed = 0, total = 0;
    bool can_cancel = false;
};
} // namespace forge
