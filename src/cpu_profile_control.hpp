#pragma once
#include <forge/services.hpp>
namespace forge {
// Optional observations and controls use the existing protocol's session checks.
inline bool cpu_profile_watch(const nlohmann::json& request) {
    if (!request.contains("profile_watch"))
        return false;
    if (!request.at("profile_watch").is_boolean())
        throw std::runtime_error("profile_watch must be boolean");
    return request.at("profile_watch").get<bool>();
}
inline bool control_cpu_profile(const nlohmann::json& request, ServiceAccess services,
                                std::uint64_t generation) {
    if (!generation || !request.contains("generation") ||
        !request.at("generation").is_number_integer() || request.at("generation") <= 0 ||
        request.at("generation").get<std::uint64_t>() != generation)
        throw std::runtime_error("Profiling requires the current active world generation");
    if (!request.contains("enabled") || !request.at("enabled").is_boolean() ||
        (request.contains("clear") && !request.at("clear").is_boolean()))
        throw std::runtime_error("Profiling enabled/clear controls must be boolean");
    const bool enabled = request.at("enabled").get<bool>();
    services.require(Capability::Profiling);
    if (request.value("clear", false))
        services.clear_profiles();
    services.profiling(enabled);
    return enabled;
}
inline nlohmann::json observed_cpu_profile(ServiceAccess services, std::uint64_t generation) {
    auto result = services.profile_status();
    result["available"] = true;
    result["world_generation"] = generation;
    return result;
}
} // namespace forge
