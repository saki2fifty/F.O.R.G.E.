// Minimal malformed-output/cancellation fixture for the fixed schema command.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#ifdef __linux__
#include <sys/types.h>
#include <unistd.h>
#endif
int main(int argc, char** argv) {
    using Json = nlohmann::json;
    if (argc != 2 || std::string(argv[1]) != "--inspect-sdk-worker")
        return 2;
    const auto request = Json::parse(std::ifstream("request.json"));
    const auto root = std::filesystem::u8path(request.at("project").get<std::string>());
    std::string mode;
    std::ifstream(root / "fixture.mode") >> mode;
#ifdef __linux__
    if (mode == "escaped" || mode == "escaped-wait") {
        std::ofstream(root / "fixture.stage") << std::filesystem::current_path().string();
        const auto child = fork();
        if (child < 0)
            return 20;
        if (child == 0) {
            if (setsid() < 0)
                _exit(21);
            std::ofstream(root / "fixture.pid") << getpid();
            std::ofstream(root / "fixture.child-ready") << "ready";
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!std::filesystem::exists(root / "fixture.release")) {
                if (std::chrono::steady_clock::now() >= end)
                    _exit(22);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            _exit(0);
        }
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!std::filesystem::exists(root / "fixture.child-ready")) {
            if (std::chrono::steady_clock::now() >= end)
                return 23;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Cancellation becomes observable only after the descendant identity
        // is closed on disk, so the controller can always retire that child.
        std::ofstream(root / "fixture.ready") << "ready";
        if (mode == "escaped-wait")
            std::this_thread::sleep_for(std::chrono::seconds(60));
        std::filesystem::copy_file(root / "fixture-manifest.json", "result.json");
        return 0;
    }
#endif
    if (mode == "wait") {
        std::ofstream(root / "fixture.started") << "waiting";
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 3;
    }
    if (mode == "duplicate") {
        std::ofstream("result.json") << R"({"format":"forge.authored-types","format":"ignored"})";
        return 0;
    }
    if (mode == "large") {
        std::ofstream out("result.json", std::ios::binary);
        const std::string block(65536, 'x');
        for (unsigned i = 0; i < 280; ++i)
            out << block;
        return 0;
    }
    if (mode == "profile") {
        std::ofstream("result.json") << Json{
            {"format", "forge.authored-types"},
            {"version", 1},
            {"profile", "static-core"},
            {"fingerprint", request.at("fingerprint")},
            {"components", Json::array()}}.dump();
        return 0;
    }
    if (mode == "manifest") {
        std::filesystem::copy_file(root / "fixture-manifest.json", "result.json");
        return 0;
    }
    return 4;
}
