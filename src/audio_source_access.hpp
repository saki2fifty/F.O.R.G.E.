#pragma once
#include <forge/audio.hpp>
#include <stdexcept>
namespace forge {
// Private editor Play access; no backend object or new gameplay service crosses the wire.
struct AudioSourceAccess {
    static Json status(AudioRuntime&, EntityRef);
    static void control(AudioRuntime&, EntityRef, AudioSourceOperation);
    static void control(AudioRuntime& runtime, EntityRef source, bool play) {
        control(runtime, source, play ? AudioSourceOperation::Restart : AudioSourceOperation::Stop);
    }
    static AudioSourceOperation operation(const Json& request) {
        if (request.contains("play")) {
            if (request.contains("operation") || !request.at("play").is_boolean())
                throw std::runtime_error(
                    "Source playback requires one operation or a boolean play value");
            return request.at("play").get<bool>() ? AudioSourceOperation::Restart
                                                  : AudioSourceOperation::Stop;
        }
        const auto& value = request.at("operation");
        if (value.is_string()) {
            if (value == "stop")
                return AudioSourceOperation::Stop;
            if (value == "play")
                return AudioSourceOperation::Restart;
            if (value == "pause")
                return AudioSourceOperation::Pause;
            if (value == "resume")
                return AudioSourceOperation::Resume;
        }
        throw std::runtime_error("Unknown source playback operation");
    }
};
} // namespace forge
