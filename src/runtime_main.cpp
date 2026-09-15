#include <cmath>
#include <forge/build.hpp>
#include <forge/module.hpp>
#include <forge/scene.hpp>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "FORGE runtime | Build: " << forge::build_id << '\n';
        return 0;
    }
    std::clog << "FORGE runtime | Build: " << forge::build_id << '\n';
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    forge::Scene scene;
    forge::Module module;
    ForgeHostV1 host{sizeof(ForgeHostV1), FORGE_MODULE_API_VERSION, &scene,
                     [](void* p, float x, float y, float z) {
                         static_cast<forge::Scene*>(p)->translate(x, y, z);
                     }};
    std::string line;
    while (std::getline(std::cin, line)) {
        forge::Json response;
        bool quit = false;
        try {
            const auto request = forge::Json::parse(line);
            if (request.value("protocol", 0) != 1)
                throw std::runtime_error("Unsupported protocol version");
            const auto command = request.at("command").get<std::string>();
            if (command == "replace")
                scene.replace(request.at("scene"));
            else if (command == "step") {
                const auto dt = request.value("seconds", 1.0f / 60.0f);
                if (!std::isfinite(dt) || dt < 0 || dt > 1)
                    throw std::runtime_error("Step must be between 0 and 1 seconds");
                module.tick(host, dt);
                scene.world().progress(dt);
            } else if (command == "load_module")
                module.load(request.at("path").get<std::string>());
            else if (command == "save")
                scene.save(request.at("path").get<std::string>());
            else if (command == "quit")
                quit = true;
            else if (command != "snapshot" && command != "ping" && command != "schema")
                throw std::runtime_error("Unknown command");
            response = {{"protocol", 1},
                        {"ok", true},
                        {"module", module.id()},
                        {"scene", scene.document()},
                        {"schema", scene.schema()}};
        } catch (const std::exception& e) {
            response = {{"protocol", 1}, {"ok", false}, {"error", e.what()}};
        }
        std::cout << response.dump() << std::endl;
        if (quit)
            break;
    }
}
