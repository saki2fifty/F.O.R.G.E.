#include "code_automation.hpp"
#include "export_automation.hpp"
#include "play_automation.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
Json invoke(const std::vector<LiveAuthoringMethod>& methods, const char* name) {
    const auto method = std::find_if(methods.begin(), methods.end(),
                                     [&](const auto& value) { return value.name == name; });
    check(method != methods.end(), "Log method missing");
    return method->invoke(Json::object());
}
} // namespace
int main(int argc, char** argv) {
    // Play launches this executable without fixture arguments. This is a real
    // child process producing diagnostic bytes, not a successful runtime game.
    if (argc == 1) {
        std::string request;
        if (!std::getline(std::cin, request))
            return 2;
        const std::string output = "valid \xe7\x95\x8c invalid \xff tail\n";
        std::fwrite(output.data(), 1, output.size(), stderr);
        std::fflush(stderr);
        while (std::getline(std::cin, request)) {
        }
        return 0;
    }
    try {
        check(argc == 2, "Need log scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        check(SDL_Init(0), "SDL process setup failed");
        struct SdlCleanup {
            ~SdlCleanup() { SDL_Quit(); }
        } sdl_cleanup;
        SceneDocument::create_project(root / "Game", "Log output");
        EngineContext engine;
        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        const auto before = scene.document();
        PlaySession play;
        RuntimeTuningReview tuning;
        ui::AutomationPermissions permissions;
        const auto methods = PlayAutomation::methods(scene, files, play, tuning, {}, permissions);
        play.start(path_utf8(std::filesystem::absolute(argv[0])), scene.snapshot(), true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (play.timed_log().find("tail\n") == std::string::npos &&
               std::chrono::steady_clock::now() < deadline) {
            play.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(play.timed_log().find("\xff") != std::string::npos &&
                  play.log().find("\xff") != std::string::npos,
              "Real child diagnostic bytes did not reach the Play owner");
        const auto raw = play.timed_log();
        const auto response = invoke(methods, "play.log");
        std::string wire;
        try {
            wire = response.dump();
        } catch (const Json::type_error&) {
            throw std::runtime_error("Play log response cannot serialize actual process bytes");
        }
        const auto roundtrip = Json::parse(wire);
        check(roundtrip.at("text").get<std::string>().find("\xe7\x95\x8c") != std::string::npos &&
                  roundtrip.at("text").get<std::string>().find("\xef\xbf\xbd") != std::string::npos,
              "Log response lost valid Unicode or failed to replace malformed bytes");
        check(play.timed_log() == raw, "Reading a log changed the owner's bytes");
        play.stop();
        check(Json::parse(invoke(methods, "play.log").dump()).at("text") == roundtrip.at("text"),
              "Stopping Play lost the readable diagnostic result");

        const auto text = std::string("a\xe7\x95\x8c\xff\xe2\x82");
        const auto page = CodeAutomation::text_page(text, {{"offset", 0}, {"limit", 100}});
        check(Json::parse(page.dump()).at("text").get<std::string>().starts_with("a\xe7\x95\x8c") &&
                  page.at("next_offset") == text.size() && page.at("total_bytes") == text.size(),
              "Compiler text-page sanitization changed byte offsets");
        const auto boundary = CodeAutomation::text_page(text, {{"offset", 2}, {"limit", 1}});
        check(boundary.at("offset") == 4 && boundary.at("next_offset") == 5 &&
                  boundary.at("text") == "\xef\xbf\xbd",
              "Text-page UTF-8 boundary behavior changed");
        const std::string valid = "ASCII \xe7\x95\x8c \xf0\x9f\x98\x80\r\n\t";
        check(automation_text_value(valid) == valid, "Valid Unicode or log whitespace changed");
        for (const auto& malformed :
             {std::string("\xe2"), std::string("\xe2\x82"), std::string("\x80"),
              std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
              std::string("\xf4\x90\x80\x80")}) {
            const auto safe = automation_text_value(malformed);
            check(Json::parse(safe.dump()) == safe &&
                      safe.get<std::string>().find("\xef\xbf\xbd") != std::string::npos,
                  "Malformed native text was not safe for strict JSON serialization");
        }
        ui::GameExportTask exporter;
        check(invoke(ExportAutomation::methods(files, exporter, {}, permissions), "export.run.log")
                      .at("text") == "",
              "An unstarted export exposed another operation's log");
        check(scene.document() == before && !scene.can_undo(),
              "Diagnostic reads changed authored state");
        std::cout << "Actual Play child output remains readable through automation JSON\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
