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
        const auto send = [](const Json& value) {
            const auto reply = value.dump();
            std::cout << "Content-Length: " << reply.size() << "\r\n\r\n" << reply << std::flush;
        };
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
            Json delayed;
            if (method == "initialize") {
                const auto& signature = request.at("params")
                                            .at("capabilities")
                                            .at("textDocument")
                                            .at("signatureHelp")
                                            .at("signatureInformation");
                if (signature.at("parameterInformation").at("labelOffsetSupport").get<bool>() ||
                    signature.at("documentationFormat") != Json::array({"plaintext"}))
                    throw std::runtime_error("Signature plain-label negotiation missing");
                const auto& document = request.at("params").at("capabilities").at("textDocument");
                if (document.at("hover").at("contentFormat") != Json::array({"plaintext"}) ||
                    !document.at("documentSymbol")
                         .at("hierarchicalDocumentSymbolSupport")
                         .get<bool>())
                    throw std::runtime_error(
                        "Plain symbol information/hierarchical outline negotiation missing");
                result = {
                    {"serverInfo",
                     {{"name", "clangd"},
                      {"version",
                       mode == "wrong-version"
                           ? "unsupported"
                           : "clangd version 23.1.0 (ea7d852a70e8bdfaf601d6626a760f9771b2c4b4)"}}},
                    {"capabilities",
                     {{"positionEncoding", "utf-8"},
                      {"documentSymbolProvider", true},
                      {"signatureHelpProvider", Json::object()},
                      {"hoverProvider", mode != "hover-unavailable"}}}};
                if (mode == "signature-unavailable")
                    result["capabilities"].erase("signatureHelpProvider");
            } else if (method == "textDocument/documentSymbol") {
                ready = request.at("params").at("textDocument").at("uri").get<std::string>();
                result = Json::array();
                const auto& text = drafts.at(ready);
                const std::string field = text.find("outline_member") != text.npos
                                              ? "outline_member"
                                              : "degrees_per_second";
                const auto field_begin = text.find(field);
                if (field_begin != text.npos) {
                    const Json range{
                        {"start", forge::cpp_semantic_position(text, field_begin)},
                        {"end", forge::cpp_semantic_position(text, field_begin + field.size())}};
                    Json item{{"name", field},
                              {"detail", "float"},
                              {"kind", 8},
                              {"range", range},
                              {"selectionRange", range}};
                    Json parent{{"name", "Fixture scope"},
                                {"kind", 3},
                                {"range",
                                 {{"start", forge::cpp_semantic_position(text, 0)},
                                  {"end", forge::cpp_semantic_position(text, text.size())}}},
                                {"selectionRange",
                                 {{"start", forge::cpp_semantic_position(text, 0)},
                                  {"end", forge::cpp_semantic_position(text, 0)}}},
                                {"children", Json::array({item})}};
                    result = Json::array({parent});
                    if (mode == "symbols-empty")
                        result = Json::array();
                    else if (mode == "symbols-range")
                        result[0]["children"][0]["selectionRange"] = parent["selectionRange"];
                    else if (mode == "symbols-kind")
                        result[0]["kind"] = 27;
                    else if (mode == "symbols-label")
                        result[0]["name"] = std::string(2049, 'x');
                    else if (mode == "symbols-limited") {
                        result = Json::array();
                        for (int i = 0; i != 65; ++i)
                            result.push_back(item);
                    } else if (mode == "symbols-depth") {
                        for (int i = 0; i != 17; ++i) {
                            parent["children"] = result;
                            result = Json::array({parent});
                        }
                    }
                }
                const auto begin = text.find("degrees_per_second");
                Json diagnostics = Json::array();
                if (begin != text.npos) {
                    Json item{{"range",
                               {{"start", forge::cpp_semantic_position(text, begin)},
                                {"end", forge::cpp_semantic_position(text, begin + 18)}}},
                              {"severity", 2},
                              {"code", "fixture-warning"},
                              {"message", "Test warning for current source."}};
                    diagnostics.push_back(item);
                    if (mode == "diagnostic-limited")
                        for (int i = 1; i != 65; ++i)
                            diagnostics.push_back(item);
                }
                if (mode == "diagnostic-empty")
                    diagnostics = Json::array();
                if (mode == "diagnostic-range")
                    diagnostics[0]["range"]["start"]["character"] = 999999;
                if (mode == "diagnostic-severity")
                    diagnostics[0]["severity"] = 5;
                if (mode == "diagnostic-oversized")
                    diagnostics[0]["message"] = std::string(8193, 'x');
                Json notification{
                    {"jsonrpc", "2.0"},
                    {"method", "textDocument/publishDiagnostics"},
                    {"params", {{"uri", ready}, {"version", 1}, {"diagnostics", diagnostics}}}};
                if (mode == "diagnostic-version")
                    notification["params"]["version"] = 0;
                if (mode == "diagnostic-unversioned")
                    notification["params"].erase("version");
                if (mode == "diagnostic-external") {
                    auto external = notification;
                    external["params"]["uri"] = forge::local_file_url(
                        std::filesystem::current_path().parent_path() / "Outside/Private.hpp");
                    send(external);
                }
                if (mode == "diagnostic-late")
                    delayed = notification;
                else if (mode != "diagnostic-missing")
                    send(notification);
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
            } else if (method == "textDocument/hover") {
                const auto& params = request.at("params");
                if (ready != params.at("textDocument").at("uri").get<std::string>())
                    throw std::runtime_error("Symbol information ran before document readiness");
                const auto& text = drafts.at(ready);
                const auto begin = forge::cpp_semantic_offset(text, params.at("position"));
                result = {
                    {"contents", {{"kind", "plaintext"}, {"value", "Current symbol\nType: float"}}},
                    {"range",
                     {{"start", forge::cpp_semantic_position(text, begin)},
                      {"end", forge::cpp_semantic_position(text, begin)}}}};
                if (mode == "hover-null")
                    result = nullptr;
                else if (mode == "hover-markdown")
                    result["contents"]["kind"] = "markdown";
                else if (mode == "hover-range")
                    result["range"]["start"]["character"] = 999999;
                else if (mode == "hover-oversized")
                    result["contents"]["value"] = std::string(8193, 'x');
                else if (mode == "hover-no-range")
                    result.erase("range");
            } else if (method == "textDocument/signatureHelp") {
                if (ready != request.at("params").at("textDocument").at("uri").get<std::string>())
                    throw std::runtime_error("Signature ran before document readiness");
                Json item{{"label", "throttle(float café, float 𐐀rate) -> void"},
                          {"documentation", {{"kind", "plaintext"}, {"value", "Adjust speed."}}},
                          {"parameters", Json::array({{{"label", "float café"}},
                                                      {{"label", "float 𐐀rate"},
                                                       {"documentation", "Rate to apply."}}})}};
                result = {{"signatures", Json::array({item, {{"label", "throttle() -> void"}}})},
                          {"activeSignature", 0},
                          {"activeParameter", 1}};
                if (mode == "signature-null")
                    result = nullptr;
                else if (mode == "signature-empty")
                    result["signatures"] = Json::array();
                else if (mode == "signature-offsets")
                    result["signatures"][0]["parameters"][0]["label"] = Json::array({9, 19});
                else if (mode == "signature-markdown")
                    result["signatures"][0]["documentation"]["kind"] = "markdown";
                else if (mode == "signature-bad-index")
                    result["activeSignature"] = 4;
                else if (mode == "signature-fractional")
                    result["activeParameter"] = 0.5;
                else if (mode == "signature-oversized")
                    result["signatures"][0]["label"] = std::string(4097, 'x');
                else if (mode == "signature-zero") {
                    result["signatures"] = Json::array({{{"label", "throttle() -> void"}}});
                    result["activeParameter"] = 0;
                } else if (mode == "signature-limited") {
                    result["signatures"] = Json::array();
                    for (int i = 0; i != 65; ++i)
                        result["signatures"].push_back(item);
                    result["activeSignature"] = 64;
                }
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
            if (mode == "bad-frame") {
                std::cout << "Content-Length: 9000000\r\n\r\n" << std::flush;
                return 0;
            }
            send(Json{{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", result}});
            if (!delayed.is_null())
                send(delayed);
            if (mode == "diagnostic-missing" && method == "textDocument/documentSymbol")
                return 0;
        }
    } catch (const std::exception& error) {
        std::cerr << "Semantic protocol fixture: " << error.what() << '\n';
        return 3;
    }
}
