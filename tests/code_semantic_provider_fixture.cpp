#include "code_semantic.hpp"
#include "local_file_url.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// Deterministic protocol peer, not a semantic compiler. Real-provider evidence
// uses a separately installed tool; this fixture exercises trust boundaries.
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    using Json = nlohmann::json;
    try {
        const auto* selected = std::getenv("FORGE_SEMANTIC_FIXTURE_CASE");
        const std::string mode = selected ? selected : "normal";
        std::map<std::string, std::string> drafts;
        std::string ready;
        for (;;) {
            std::string line;
            if (!std::getline(std::cin, line))
                return 1;
            const auto size = std::stoul(line.substr(std::string("Content-Length: ").size()));
            if (size > 8 * 1024 * 1024 || !std::getline(std::cin, line) || line != "\r")
                return 2;
            std::string bytes(size, '\0');
            std::cin.read(bytes.data(), static_cast<std::streamsize>(size));
            const auto request = Json::parse(bytes);
            const auto method = request.at("method").get<std::string>();
            if (method == "exit")
                return mode == "exit-failure" ? 7 : 0;
            if (method == "textDocument/didOpen") {
                const auto& document = request.at("params").at("textDocument");
                drafts[document.at("uri").get<std::string>()] =
                    document.at("text").get<std::string>();
                continue;
            }
            if (!request.contains("id"))
                continue;
            Json result;
            if (method == "initialize") {
                result = {
                    {"serverInfo",
                     {{"name", "clangd"},
                      {"version",
                       mode == "wrong-version"
                           ? "unsupported"
                           : "clangd version 23.1.0 (ea7d852a70e8bdfaf601d6626a760f9771b2c4b4)"}}},
                    {"capabilities",
                     {{"positionEncoding", "utf-8"}, {"documentSymbolProvider", true}}}};
            } else if (method == "textDocument/documentSymbol") {
                ready = request.at("params").at("textDocument").at("uri").get<std::string>();
                result = Json::array();
            } else if (method == "textDocument/completion") {
                if (ready != request.at("params").at("textDocument").at("uri").get<std::string>())
                    throw std::runtime_error("Completion ran before initial document readiness");
                const auto& params = request.at("params");
                const auto& text =
                    drafts.at(params.at("textDocument").at("uri").get<std::string>());
                const auto end = forge::cpp_semantic_offset(text, params.at("position"));
                const auto begin = end >= 2 ? end - 2 : end;
                Json edit{{"range",
                           {{"start", forge::cpp_semantic_position(text, begin)},
                            {"end", forge::cpp_semantic_position(text, end)}}},
                          {"newText", "degrees_per_second"}};
                Json item{{"label", "degrees_per_second"},
                          {"detail", "float Spinner member"},
                          {"textEdit", edit}};
                if (mode == "unsupported")
                    item["command"] = {{"command", "must-never-run"}};
                if (mode == "invalid-range")
                    item["textEdit"]["range"]["start"]["character"] = 999999;
                result = {{"items", Json::array({item})}, {"isIncomplete", false}};
            } else if (method == "textDocument/declaration") {
                const auto uri =
                    request.at("params").at("textDocument").at("uri").get<std::string>();
                const auto& text = drafts.at(uri);
                const auto begin = text.find("degrees_per_second");
                result = Json::array(
                    {{{"uri",
                       mode == "external"
                           ? forge::local_file_url(std::filesystem::current_path().parent_path() /
                                                   "Outside/Private.hpp")
                           : uri},
                      {"range",
                       {{"start", forge::cpp_semantic_position(text, begin)},
                        {"end", forge::cpp_semantic_position(text, begin + 18)}}}}});
            }
            const auto reply =
                Json{{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", result}}.dump();
            if (mode == "bad-frame") {
                std::cout << "Content-Length: 9000000\r\n\r\n" << std::flush;
                return 0;
            }
            std::cout << "Content-Length: " << reply.size() << "\r\n\r\n" << reply << std::flush;
        }
    } catch (const std::exception& error) {
        std::cerr << "Semantic protocol fixture: " << error.what() << '\n';
        return 3;
    }
}
