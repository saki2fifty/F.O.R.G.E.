#include "asset_tools_cli.hpp"
#include "ecs_tools_cli.hpp"
#include <forge/authoring.hpp>
#include <forge/build.hpp>
#include <forge/flecs_script.hpp>
#include <forge/project_paths.hpp>
#include <forge/schema.hpp>
#include <fstream>
#include <iostream>
// Authoring stdio is memory-only; asset commands inspect project files read-only.
int main(int argc, char** argv) {
    ecs_os_set_api_defaults();
    ecs_os_api.log_out_ = stderr; // Keep native diagnostics outside the JSON protocol.
    if (argc >= 2 && std::string(argv[1]) == "--assets")
        return forge::asset_tools_cli(argc, argv);
    if (argc == 2 && std::string(argv[1]) == "--ecs-stdio")
        return forge::ecs_tools_stdio();
    if (argc == 4 && std::string(argv[1]) == "--script") {
        try {
            forge::ProjectPaths paths(std::filesystem::absolute(std::filesystem::u8path(argv[2])));
            const auto source = forge::ProjectPaths::normalize(std::filesystem::u8path(argv[3]));
            const auto code = forge::read_flecs_script_source(paths.root(), source);
            const auto result = forge::preview_flecs_script(
                paths.root(), source, code, {},
                std::filesystem::absolute(std::filesystem::u8path(argv[0])));
            std::cout << result.dump(2) << '\n';
            return result.at("ok").get<bool>() ? 0 : 1;
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string(argv[1]) == "--script-worker") {
        try {
            if (std::filesystem::file_size("request.json") > 3 * 1024 * 1024)
                throw std::runtime_error("Script request is too large");
            std::ifstream input("request.json", std::ios::binary);
            const auto result = forge::evaluate_flecs_script_worker(forge::Json::parse(input));
            forge::atomic_write("result.json", result.dump());
            return 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "FORGE tools | Build: " << forge::build_id << '\n';
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--document-schemas") {
        std::cout << forge::core_document_schemas().describe().dump(2) << '\n';
        return 0;
    }
    if (argc != 2 || std::string(argv[1]) != "--stdio") {
        std::cout << "FORGE tools --stdio | --ecs-stdio | --script PROJECT SOURCE.flecs\n"
                     "Asset inspection: --assets scan PROJECT [ROOT] | query PROJECT | dependents "
                     "PROJECT UUID\nAuthoring "
                     "API 1, one JSON request/response per "
                     "line.\nStart with {\"api\":1,\"method\":\"discover\"}. Memory-only; no "
                     "project writes or native execution.\n";
        return argc == 1 ? 0 : 2;
    }
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    forge::AuthoringSession session(scene);
    constexpr std::size_t limit = 1024 * 1024;
    while (std::cin) {
        std::string line;
        char ch = 0;
        bool overflow = false;
        while (std::cin.get(ch) && ch != '\n') {
            if (line.size() < limit)
                line.push_back(ch);
            else
                overflow = true;
        }
        if (line.empty() && !overflow && !std::cin)
            break;
        forge::Json response;
        try {
            if (overflow)
                throw std::runtime_error("Request exceeds 1 MiB");
            const auto request =
                forge::Json::parse(line, [](int depth, forge::Json::parse_event_t, forge::Json&) {
                    if (depth > 64)
                        throw std::runtime_error("Request nesting exceeds 64 levels");
                    return true;
                });
            response = session.handle(request);
        } catch (const std::exception& e) {
            response = {{"api", 1},
                        {"ok", false},
                        {"error", {{"code", "invalid_request"}, {"message", e.what()}}}};
        }
        std::cout << response.dump() << std::endl;
    }
    return 0;
}
