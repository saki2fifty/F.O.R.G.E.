#include <forge/authoring.hpp>
#include <forge/build.hpp>
#include <iostream>
// Deliberately memory-only: this process cannot compete with an editor for project files.
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "FORGE tools | Build: " << forge::build_id << '\n';
        return 0;
    }
    if (argc != 2 || std::string(argv[1]) != "--stdio") {
        std::cout << "FORGE tools --stdio\nAuthoring API 1, one JSON request/response per "
                     "line.\nStart with {\"api\":1,\"method\":\"discover\"}. Memory-only; no "
                     "project writes or native execution.\n";
        return argc == 1 ? 0 : 2;
    }
    forge::Scene scene;
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
