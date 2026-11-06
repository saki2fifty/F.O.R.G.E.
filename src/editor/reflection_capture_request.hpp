#pragma once
#include "../reflection_capture_settings.hpp"
#include <filesystem>
namespace forge::ui {
struct ReflectionCaptureRequest {
    ReflectionCaptureSettings settings;
    std::filesystem::path destination = "Assets/ReflectionMap";
    bool assign = true;
    bool update_assigned = false;
};
} // namespace forge::ui
