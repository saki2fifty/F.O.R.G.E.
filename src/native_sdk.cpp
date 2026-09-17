#include <cstring>
#include <forge/native_sdk.h>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project_paths.hpp>
#include <set>
#ifdef FORGE_ENABLE_NATIVE_SDK
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif
namespace forge {
namespace {
const std::set<std::string> builtins{"forge.core", "forge.transforms", "forge.prefabs",
                                     "forge.input"};
#ifdef FORGE_ENABLE_NATIVE_SDK
struct Library {
    void* handle{};
    ~Library() {
#ifdef _WIN32
        if (handle)
            FreeLibrary(static_cast<HMODULE>(handle));
#else
        if (handle)
            dlclose(handle);
#endif
    }
};
std::string bounded(const char* p, std::size_t maximum) {
    if (!p)
        throw std::runtime_error("Null native SDK metadata");
    std::size_t n = 0;
    while (n <= maximum && p[n])
        ++n;
    if (!n || n > maximum)
        throw std::runtime_error("Invalid native SDK metadata length");
    return {p, n};
}
struct Bridge {
    ModuleContext& context;
    ForgeSdkWorldV1 host{};
    explicit Bridge(ModuleContext& c) : context(c) {
        host.size = sizeof(host);
        host.version = FORGE_NATIVE_SDK_ABI;
        host.world = c.world.c_ptr();
        host.context = this;
        host.role = role_mask(c.role);
        for (auto cap : {Capability::Diagnostics, Capability::Profiling})
            if (c.services.available(cap))
                host.capabilities |= capability(cap);
        host.fixed_phase = c.world.entity("forge.runtime.Gameplay").add(flecs::Phase).id();
        host.fixed_tag = c.world.id<FixedSimulation>();
        host.read_action = [](void* p, const char* id, ForgeSdkActionV1* out) -> int32_t {
            try {
                auto& c = static_cast<Bridge*>(p)->context;
                if (!out || out->size != sizeof(*out) || !c.input)
                    return 0;
                auto action = ActionId::parse(bounded(id, 36));
                const auto it = c.input->actions.find(action);
                if (it == c.input->actions.end())
                    return 0;
                const auto& a = it->second;
                *out = {
                    sizeof(*out), uint32_t(a.held), uint32_t(a.pressed), uint32_t(a.released), a.x,
                    a.y,          c.input->tick};
                return 1;
            } catch (...) {
                return 0;
            }
        };
        host.diagnostic = [](void* p, uint32_t severity, const char* text) -> int32_t {
            try {
                auto& c = static_cast<Bridge*>(p)->context;
                if (severity > 4 || !c.services.available(Capability::Diagnostics))
                    return 0;
                Diagnostic d{static_cast<Severity>(severity), "module", bounded(text, 8192), {}};
                d.context.module = c.id;
                d.context.world_role = world_role_name(c.role);
                if (c.input)
                    d.context.tick = c.input->tick;
                c.services.emit(std::move(d));
                return 1;
            } catch (...) {
                return 0;
            }
        };
    }
};
#endif
} // namespace
void validate_project_modules(const nlohmann::json& project) {
    if (!project.contains("modules"))
        return;
    const auto& modules = project.at("modules");
    if (!modules.is_array() || modules.size() > 64)
        throw std::runtime_error("Project modules require an array of at most 64 declarations");
    std::set<std::string> seen;
    std::vector<ModuleRequirement> graph;
    for (const auto& id : builtins)
        graph.push_back({id, {}, 0});
    for (const auto& m : modules) {
        const auto id = canonical_module_id(m.is_string() ? m.get<std::string>()
                                                          : m.at("id").get<std::string>());
        if (!valid_module_id(id) || !seen.insert(id).second)
            throw std::runtime_error("Invalid/duplicate project module: " + id);
        if (m.is_string()) {
            if (!builtins.contains(id))
                throw std::runtime_error("Unknown built-in module: " + id);
            continue;
        }
        if (builtins.contains(id) || m.at("sdk") != "experimental-1" ||
            m.at("implementation").get<std::string>().empty())
            throw std::runtime_error("Invalid native project module declaration: " + id);
        const auto fingerprint = m.at("fingerprint").get<std::string>();
        if (fingerprint.size() != 64 ||
            fingerprint.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Invalid SDK fingerprint: " + id);
        (void)ProjectPaths::normalize(std::filesystem::u8path(m.at("library").get<std::string>()));
        auto deps = m.at("dependencies").get<std::vector<std::string>>();
        if (deps.size() > 64 ||
            std::set<std::string>(deps.begin(), deps.end()).size() != deps.size())
            throw std::runtime_error("Invalid project module dependencies");
        graph.push_back({id, std::move(deps), 0});
    }
    (void)module_order(graph, 0);
}
EngineModule load_native_sdk(const std::filesystem::path& path, const std::string& id,
                             const std::string& implementation, ServiceAccess services) {
    try {
#ifndef FORGE_ENABLE_NATIVE_SDK
        (void)path;
        (void)implementation;
        throw std::runtime_error(
            "Direct Flecs modules require the shared native-sdk build profile");
#else
        if (!path.is_absolute() || !valid_module_id(id))
            throw std::runtime_error(
                "SDK library requires an absolute path and namespaced module ID");
        auto code = std::make_shared<Library>();
#ifdef _WIN32
        code->handle =
            LoadLibraryExW(path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        auto entry = code->handle ? reinterpret_cast<ForgeNativeSdkEntryV1>(GetProcAddress(
                                        static_cast<HMODULE>(code->handle), "forge_native_sdk_v1"))
                                  : nullptr;
#else
        code->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        auto entry = code->handle ? reinterpret_cast<ForgeNativeSdkEntryV1>(
                                        dlsym(code->handle, "forge_native_sdk_v1"))
                                  : nullptr;
#endif
        if (!entry)
            throw std::runtime_error("Cannot load native SDK library/entry");
        const auto* api = entry();
        if (!api || api->size != sizeof(*api) || api->version != FORGE_NATIVE_SDK_ABI ||
            !api->register_schema)
            throw std::runtime_error("Incompatible native SDK descriptor ABI");
        if (bounded(api->fingerprint, 64) != FORGE_NATIVE_SDK_FINGERPRINT)
            throw std::runtime_error("Native SDK fingerprint mismatch");
        if (api->flecs_init_identity != &ecs_init || api->flecs_os_identity != &ecs_os_api)
            throw std::runtime_error("Native module does not share the host Flecs implementation");
        if (bounded(api->module_id, 128) != id ||
            bounded(api->implementation_version, 128) != implementation)
            throw std::runtime_error("Native module identity/implementation mismatch");
        if (api->dependency_count > 64 || (api->dependency_count && !api->dependencies))
            throw std::runtime_error("Invalid native module dependencies");
        EngineModule result;
        result.id = id;
        result.implementation = implementation;
        result.code = code;
        for (uint32_t i = 0; i < api->dependency_count; ++i)
            result.dependencies.push_back(bounded(api->dependencies[i], 128));
        if (std::set<std::string>(result.dependencies.begin(), result.dependencies.end()).size() !=
            result.dependencies.size())
            throw std::runtime_error("Duplicate native module dependency");
        result.schema_roles = api->schema_roles;
        result.runtime_roles = api->runtime_roles;
        result.required_services = api->required_capabilities;
        result.allowed_services = api->allowed_capabilities;
        result.schemas = [api](ModuleContext& c) {
            auto bridge = std::make_shared<Bridge>(c);
            c.state = bridge;
            char error[1024]{};
            if (!api->register_schema(&bridge->host, error, sizeof(error))) {
                error[1023] = 0;
                throw std::runtime_error(std::string("Native schema registration failed: ") +
                                         error);
            }
        };
        result.start = [api](ModuleContext& c) {
            char error[1024]{};
            if (api->start &&
                !api->start(&static_cast<Bridge*>(c.state.get())->host, error, sizeof(error))) {
                error[1023] = 0;
                throw std::runtime_error(std::string("Native runtime registration failed: ") +
                                         error);
            }
        };
        result.stop = [api](ModuleContext& c) {
            if (api->stop && c.state)
                api->stop(&static_cast<Bridge*>(c.state.get())->host);
        };
        return result;
#endif
    } catch (const std::exception& e) {
        if (services.available(Capability::Diagnostics)) {
            Diagnostic d{Severity::Error, "module", e.what(), {}};
            d.context.module = id;
            d.context.source = path_utf8(path);
            services.emit(std::move(d));
        }
        throw;
    }
}
std::vector<EngineModule> project_native_modules(const std::filesystem::path& root,
                                                 const nlohmann::json& project,
                                                 ServiceAccess services) {
    validate_project_modules(project);
    std::vector<EngineModule> result;
    for (const auto& m : project.value("modules", nlohmann::json::array()))
        if (m.is_object()) {
            if (m.at("fingerprint") != FORGE_NATIVE_SDK_FINGERPRINT)
                throw std::runtime_error("Project module SDK fingerprint mismatch: " +
                                         m.at("id").get<std::string>());
            auto module = load_native_sdk(ProjectPaths(root).resolve(std::filesystem::u8path(
                                              m.at("library").get<std::string>())),
                                          m.at("id"), m.at("implementation"), services);
            if (module.dependencies != m.at("dependencies").get<std::vector<std::string>>())
                throw std::runtime_error("Module dependency declaration does not match library");
            result.push_back(std::move(module));
        }
    return result;
}
} // namespace forge
