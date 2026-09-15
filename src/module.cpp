#include <forge/module.hpp>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace forge {
namespace {
void close_library(void* library) {
    if (!library)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}
} // namespace
Module::~Module() { close_library(library_); }
void Module::load(const std::filesystem::path& path) {
    if (!path.is_absolute())
        throw std::runtime_error("Module path must be absolute");
#ifdef _WIN32
    void* candidate = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto entry = candidate ? reinterpret_cast<ForgeModuleEntry>(
                                 GetProcAddress(static_cast<HMODULE>(candidate), "forge_module_v1"))
                           : nullptr;
#else
    void* candidate = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto entry = candidate ? reinterpret_cast<ForgeModuleEntry>(dlsym(candidate, "forge_module_v1"))
                           : nullptr;
#endif
    if (!candidate)
        throw std::runtime_error("Cannot load native module");
    try {
        if (!entry)
            throw std::runtime_error("Missing forge_module_v1 entry point");
        const auto* next = entry();
        if (!next || next->size != sizeof(ForgeModuleV1) ||
            next->version != FORGE_MODULE_API_VERSION || !next->module_id || !next->state_schema ||
            !next->tick)
            throw std::runtime_error("Incompatible module ABI");
        std::string id = next->module_id, schema = next->state_schema;
        if (id.empty() || schema.empty())
            throw std::runtime_error("Missing module identity/schema");
        if (api_ && (id != id_ || schema != schema_))
            throw std::runtime_error(
                "Play restart required: module identity or state schema changed");
        // v1 has no module-owned instances, hooks or threads; tick is quiescent here.
        auto* previous = library_;
        library_ = candidate;
        api_ = next;
        id_ = std::move(id);
        schema_ = std::move(schema);
        candidate = nullptr;
        close_library(previous);
    } catch (...) {
        close_library(candidate);
        throw;
    }
}
void Module::tick(const ForgeHostV1& host, float seconds) const {
    if (api_)
        api_->tick(&host, seconds);
}
} // namespace forge
