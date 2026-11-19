#pragma once
#include "json_output_text.hpp"
#include "self_executable.hpp"
#include <forge/navigation.hpp>

namespace forge {
// Trusted hosts locate the exact worker beside their own executable. Game/agent
// requests never select an executable, shell command or staging directory.
inline std::filesystem::path runtime_navigation_worker() {
    auto worker = self_executable().parent_path() / "forge_nav_build";
#ifdef _WIN32
    worker += ".exe";
#endif
    return worker;
}
struct NavigationRebuildAccess {
    static Json state(NavigationRuntime& owner) {
        const auto value = owner.rebuild_state();
        if (!value)
            return nullptr;
        return {{"token", value->token},
                {"asset", value->asset},
                {"phase", navigation_rebuild_phase_name(value->phase)},
                {"geometry_revision", value->geometry_revision},
                {"query_generation", value->query_generation},
                {"retiring",
                 value->phase >= NavigationRebuildPhase::Applied && owner.rebuild_pending()},
                {"diagnostic", detail::json_output_text(value->diagnostic)}};
    }
    static Json control(NavigationRuntime& owner, const Json& request, std::uint64_t generation) {
        if (!generation || !request.at("generation").is_number_unsigned() ||
            request.at("generation").get<std::uint64_t>() != generation)
            throw std::runtime_error("Navigation rebuild targets a stale world generation");
        const auto operation = request.at("operation").get<std::string>();
        if (operation == "start")
            (void)owner.request_rebuild({request.at("asset").get<AssetId>()});
        else if (operation == "cancel") {
            if (!request.at("token").is_number_unsigned())
                throw std::runtime_error(
                    "Navigation cancellation requires an opaque rebuild token");
            (void)owner.cancel_rebuild(request.at("token").get<std::uint64_t>());
        } else
            throw std::runtime_error("Unknown navigation rebuild operation");
        return state(owner);
    }
};
} // namespace forge
