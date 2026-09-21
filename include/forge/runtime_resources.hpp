#pragma once
#include <filesystem>
#include <forge/engine_module.hpp>
namespace forge {
// Cooked resources only: no editor, source converter or graphics device.
EngineModule runtime_resources_module(std::filesystem::path project);
} // namespace forge
