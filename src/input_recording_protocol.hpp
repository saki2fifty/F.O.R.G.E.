#pragma once
#include "asset_bytes.hpp"
#include <forge/build.hpp>
#include <forge/input.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <span>

namespace forge {
inline std::string input_recording_source(const nlohmann::json& scene,
                                          const std::filesystem::path& project) {
    const auto encoded =
        nlohmann::json{
            {"scene", scene},
            {"settings", project.empty() ? nlohmann::json() : ProjectSettings(project).document()},
            {"build", source_commit},
            {"sdk", FORGE_NATIVE_SDK_FINGERPRINT}}
            .dump();
    return asset_detail::content_digest(std::as_bytes(std::span(encoded)));
}
// Both Play hosts use the same fixed-input owner and admission rules. This is
// a transient observation/control operation; it never edits authored history.
inline nlohmann::json input_recording_command(const nlohmann::json& request, RuntimeInput& input,
                                              const nlohmann::json& timing,
                                              const std::string& source, std::uint64_t generation) {
    const auto& target = request.at("generation");
    if (!generation || !target.is_number_integer() || target < 1 || target != generation)
        throw std::runtime_error("Input recording targets a stale world generation");
    const auto operation = request.at("operation").get<std::string>();
    if (operation == "begin" || operation == "replay") {
        if (!timing.at("paused").get<bool>() || timing.at("tick") != 0)
            throw std::runtime_error("Input recording/replay requires fresh paused tick-zero Play");
        const auto hz = timing.at("simulation_hz").get<double>();
        if (operation == "begin")
            input.begin_input_recording(source, hz);
        else
            input.replay_input_recording(request.at("document"), source, hz);
    } else if (operation == "finish")
        input.finish_input_recording();
    else if (operation == "read") {
        const auto mode = input.input_recording_status().at("mode");
        if (mode != "recorded" && mode != "finished" && mode != "canceled" && mode != "failed")
            throw std::runtime_error("Finish input recording before reading its document");
        validate_input_recording(input.input_recording_document());
        return input.input_recording_document();
    } else
        throw std::runtime_error("Unknown input recording operation");
    return nullptr;
}
} // namespace forge
