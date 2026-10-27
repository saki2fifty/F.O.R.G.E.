#include "flecs_system_trace.hpp"
#include <flecs.h>
#include <forge/flecs_script.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
#include <iostream>
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void external_trace(const char*, std::size_t, const char*) {}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need mode and scratch path");
        const std::string_view mode(argv[1]);
        if (mode == "external") {
            ecs_os_set_api_defaults();
            auto api = ecs_os_get_api();
            const auto open = api.fopen_;
            api.perf_trace_push_ = external_trace;
            ecs_os_set_api(&api);
            check(!forge::detail::initialize_native_system_tracing() &&
                      ecs_os_api.perf_trace_push_ == external_trace && ecs_os_api.fopen_ == open,
                  "Native tracing replaced the external OS policy");
            forge::EngineServices engine;
            const auto services = engine.access();
            check(!services.profiling_systems_available(),
                  "Custom native callbacks gained false support");
            bool rejected = false;
            try {
                services.profiling_systems(true);
            } catch (...) {
                rejected = true;
            }
            check(rejected && !services.profiling_systems_enabled(),
                  "Unsupported native capture was enabled");
            services.profiling(true);
            {
                auto scope = services.profile("host", "Supported host scope");
            }
            check(services.profiles().size() == 1,
                  "Unsupported native capture disabled ordinary CPU profiling");
        } else {
            check(mode == "worker" || mode == "conflict", "Unknown mode");
            if (mode == "conflict")
                check(forge::detail::initialize_native_system_tracing(),
                      "Native callback initialization failed");
            const auto open = ecs_os_api.fopen_;
            const auto root = std::filesystem::u8path(argv[2]) / forge::AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            std::ofstream(root / "Assets/main.flecs") << "Hello {}\n";
            const auto result =
                forge::evaluate_flecs_script_worker({{"project", forge::path_utf8(root)},
                                                     {"source", "Assets/main.flecs"},
                                                     {"code", "Hello {}\n"}});
            if (mode == "worker")
                check(result.at("ok") == true && !forge::detail::native_system_tracing_available(),
                      "Fresh isolated Script worker lost its confined evaluation");
            else
                check(result.at("ok") == false && ecs_os_api.fopen_ == open &&
                          result.at("error").get<std::string>().find("confined filesystem") !=
                              std::string::npos,
                      "Preconfigured native API silently bypassed Script confinement");
            std::filesystem::remove_all(root);
        }
        std::cout << "Native OS API/Script confinement boundary passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
