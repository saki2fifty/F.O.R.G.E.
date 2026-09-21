// Minimal malformed-output/cancellation fixture for the fixed schema command.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
int main(int argc, char** argv) {
    using Json = nlohmann::json;
    if (argc != 2 || std::string(argv[1]) != "--inspect-sdk-worker")
        return 2;
    const auto request = Json::parse(std::ifstream("request.json"));
    const auto root = std::filesystem::u8path(request.at("project").get<std::string>());
    std::string mode;
    std::ifstream(root / "fixture.mode") >> mode;
    if (mode == "wait") {
        std::ofstream("started.txt") << "waiting";
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
            {"profile", "static-abi1"},
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
