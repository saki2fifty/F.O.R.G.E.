#include "code_semantic.hpp"
#include "local_file_url.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {
bool identifier_byte(unsigned char c) {
    return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}
std::pair<std::size_t, std::size_t> fixture_identifier(const std::string& text,
                                                       std::size_t offset) {
    auto begin = offset, end = offset;
    while (begin && identifier_byte(text[begin - 1]))
        --begin;
    while (end < text.size() && identifier_byte(text[end]))
        ++end;
    return {begin, end};
}
} // namespace

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
                      {"referencesProvider", mode != "locations-unavailable"},
                      {"definitionProvider", mode != "locations-unavailable"},
                      {"implementationProvider", mode != "locations-unavailable"},
                      {"selectionRangeProvider", mode != "selection-unavailable"},
                      {"renameProvider", mode != "rename-unavailable"},
                      {"codeActionProvider",
                       {{"codeActionKinds",
                         mode == "fixes-unavailable" ? Json::array() : Json::array({"quickfix"})}}},
                      {"typeDefinitionProvider", mode != "locations-unavailable"},
                      {"documentFormattingProvider", mode != "format-unavailable"},
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
                const auto missing_semicolon = text.find("1 return");
                if (missing_semicolon != text.npos && mode != "diagnostic-empty") {
                    diagnostics.push_back(
                        {{"range",
                          {{"start", forge::cpp_semantic_position(text, missing_semicolon + 2)},
                           {"end", forge::cpp_semantic_position(text, missing_semicolon + 8)}}},
                         {"severity", 1},
                         {"code", "fixture-semicolon"},
                         {"message", "Expected semicolon"}});
                }
                if (mode == "diagnostic-range")
                    diagnostics[0]["range"]["start"]["character"] = 999999;
                if (mode == "diagnostic-severity")
                    diagnostics[0]["severity"] = 5;
                if (mode == "diagnostic-disordered")
                    std::reverse(diagnostics.begin(), diagnostics.end());
                if (mode == "diagnostic-duplicate" && !diagnostics.empty())
                    diagnostics.push_back(diagnostics.at(0));
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
            } else if (method == "textDocument/rename") {
                const auto& parameters = request.at("params");
                const auto uri = parameters.at("textDocument").at("uri").get<std::string>();
                const auto& text = drafts.at(uri);
                const auto offset = forge::cpp_semantic_offset(text, parameters.at("position"));
                const auto [first, last] = fixture_identifier(text, offset);
                const auto word = text.substr(first, last - first);
                if (word.empty())
                    throw std::runtime_error("Choose a fixture source name");
                Json edits = Json::array();
                for (std::size_t from = 0;;) {
                    const auto begin = text.find(word, from);
                    if (begin == text.npos)
                        break;
                    const auto end = begin + word.size();
                    from = end;
                    if ((begin && identifier_byte(text[begin - 1])) ||
                        (end < text.size() && identifier_byte(text[end])))
                        continue;
                    edits.push_back({{"range",
                                      {{"start", forge::cpp_semantic_position(text, begin)},
                                       {"end", forge::cpp_semantic_position(text, end)}}},
                                     {"newText", parameters.at("newName")}});
                }
                result = {{"changes", {{uri, edits}}}};
                const auto other =
                    forge::local_file_url(std::filesystem::current_path() / "Native/Other.hpp");
                if (mode == "rename-null")
                    result = nullptr;
                else if (mode == "rename-empty")
                    result = {{"changes", Json::object()}};
                else if (mode == "rename-count")
                    result["changes"] = Json::array();
                else if (mode == "rename-other")
                    result = {{"changes", {{other, edits}}}};
                else if (mode == "rename-multi")
                    result["changes"][other] = edits;
                else if (mode == "rename-foreign")
                    result = {
                        {"changes",
                         {{forge::local_file_url(std::filesystem::current_path().parent_path() /
                                                 "Outside/Private.hpp"),
                           edits}}}};
                else if (mode == "rename-document-changes")
                    result = {{"documentChanges", Json::array()}};
                else if (mode == "rename-resource")
                    result["documentChanges"] =
                        Json::array({{{"kind", "rename"}, {"oldUri", uri}, {"newUri", other}}});
                else if (mode == "rename-annotations")
                    result["changeAnnotations"] = {
                        {"confirmation", {{"label", "Unreviewed"}, {"needsConfirmation", true}}}};
                else if (mode == "rename-annotated-edit")
                    result["changes"][uri][0]["annotationId"] = "confirmation";
                else if (mode == "rename-command")
                    result["changes"][uri][0]["command"] = {{"command", "No execution allowed"}};
                else if (mode == "rename-range")
                    result["changes"][uri].back()["range"]["end"]["character"] = 999999;
                else if (mode == "rename-overlap")
                    result["changes"][uri].push_back(edits.at(0));
                else if (mode == "rename-split") {
                    auto point = forge::cpp_semantic_position(text, text.find("\xC3\xA9"));
                    point["character"] = point["character"].get<std::size_t>() + 1;
                    result["changes"][uri].back()["range"]["start"] = point;
                } else if (mode == "rename-disordered")
                    std::reverse(result["changes"][uri].begin(), result["changes"][uri].end());
            } else if (method == "textDocument/codeAction") {
                const auto& parameters = request.at("params");
                const auto uri = parameters.at("textDocument").at("uri").get<std::string>();
                const auto& text = drafts.at(uri);
                const auto point = parameters.at("range").at("start");
                const auto offset = forge::cpp_semantic_offset(text, point);
                const auto& diagnostics = parameters.at("context").at("diagnostics");
                if (parameters.contains("position") || parameters.at("range").at("end") != point ||
                    parameters.at("context").at("only") != Json::array({"quickfix"}))
                    throw std::runtime_error(
                        "Quick fixes need a point range and quickfix-only context");
                result = Json::array();
                const auto semicolon = text.find("1 return");
                if (semicolon != text.npos && offset == semicolon + 2) {
                    if (diagnostics.size() != 1 ||
                        diagnostics.at(0).at("code").get<std::string>() != "fixture-semicolon")
                        throw std::runtime_error("Quick fix did not use its matching diagnostic");
                    const auto insertion = forge::cpp_semantic_position(text, semicolon + 1);
                    Json edit{{"range", {{"start", insertion}, {"end", insertion}}},
                              {"newText", ";"}};
                    Json action{{"title", "Insert semicolon"},
                                {"kind", "quickfix"},
                                {"isPreferred", true},
                                {"edit", {{"changes", {{uri, Json::array({edit})}}}}}};
                    if (mode == "fixes-command")
                        action["command"] = {{"command", "Do not execute"}};
                    else if (mode == "fixes-cross-file")
                        action["edit"]["changes"][forge::local_file_url(
                            std::filesystem::current_path() / "Native/Other.hpp")] =
                            Json::array({edit});
                    else if (mode == "fixes-annotations")
                        action["edit"]["changeAnnotations"] = Json::object();
                    else if (mode == "fixes-payload")
                        action["edit"]["changes"][uri][0]["command"] = "Do not execute";
                    else if (mode == "fixes-range")
                        action["edit"]["changes"][uri][0]["range"]["end"]["character"] = 999999;
                    else if (mode == "fixes-disabled")
                        action["disabled"] = {{"reason", "Unavailable in this context"}};
                    else if (mode == "fixes-resolve")
                        action["data"] = "Deferred execution is unsupported";
                    else if (mode == "fixes-kind")
                        action["kind"] = "refactor";
                    else if (mode == "fixes-title")
                        action["title"] = std::string(2049, 'x');
                    else if (mode == "fixes-no-edit")
                        action.erase("edit");
                    else if (mode == "fixes-budget")
                        action["edit"]["changes"][uri][0]["newText"] = std::string(100000, 'x');
                    result.push_back(action);
                    if (mode == "fixes-multiple") {
                        action["title"] = "Insert semicolon with note";
                        action["edit"]["changes"][uri][0]["newText"] = "; /* fixed */";
                        result.push_back(action);
                    } else if (mode == "fixes-mixed") {
                        result[0]["command"] = {{"command", "Do not execute"}};
                        result.push_back(action);
                    } else if (mode == "fixes-limited") {
                        for (int i = 1; i != 65; ++i)
                            result.push_back(action);
                    } else if (mode == "fixes-budget") {
                        for (int i = 1; i != 32; ++i)
                            result.push_back(action);
                    }
                }
                if (mode == "fixes-null")
                    result = nullptr;
                else if (mode == "fixes-empty")
                    result = Json::array();
            } else if (method == "textDocument/formatting") {
                const auto& params = request.at("params");
                if (params.contains("position") || params.at("options").at("tabSize") != 4 ||
                    params.at("options").at("insertSpaces") != true)
                    throw std::runtime_error("Whole-source formatting request shape is invalid");
                const auto& text =
                    drafts.at(params.at("textDocument").at("uri").get<std::string>());
                const auto first = text.find("float ");
                const auto last = text.find("};", first);
                const auto edit = [&](std::size_t begin, std::size_t end, std::string replacement) {
                    return Json{{"range",
                                 {{"start", forge::cpp_semantic_position(text, begin)},
                                  {"end", forge::cpp_semantic_position(text, end)}}},
                                {"newText", std::move(replacement)}};
                };
                result = Json::array({edit(first, first, "\n  "), edit(last, last, "\n")});
                if (mode == "format-null")
                    result = nullptr;
                else if (mode == "format-empty")
                    result = Json::array();
                else if (mode == "format-noop")
                    result = Json::array({edit(0, text.size(), text)});
                else if (mode == "format-object")
                    result = result.at(0);
                else if (mode == "format-reversed")
                    result = Json::array({edit(first + 5, first, "")});
                else if (mode == "format-overlap")
                    result =
                        Json::array({edit(first, first + 5, ""), edit(first + 1, first + 2, "")});
                else if (mode == "format-ambiguous")
                    result = Json::array({edit(first, first, " "), edit(first, first, "\n")});
                else if (mode == "format-range")
                    result[0]["range"]["start"]["line"] = 999999;
                else if (mode == "format-utf8") {
                    result[0]["range"]["start"] =
                        forge::cpp_semantic_position(text, text.find("é"));
                    result[0]["range"]["start"]["character"] =
                        result[0]["range"]["start"]["character"].get<int>() + 1;
                } else if (mode == "format-control")
                    result[0]["newText"] = std::string(1, '\0');
                else if (mode == "format-budget")
                    result[0]["newText"] = std::string(1024 * 1024, 'x');
                else if (mode == "format-count") {
                    const auto item = result.at(0);
                    result = Json::array();
                    for (int i = 0; i != 32769; ++i)
                        result.push_back(item);
                } else if (mode == "format-many") {
                    result = Json::array();
                    for (std::size_t from = 0, row = 0; from < text.size(); ++row) {
                        if (text.compare(from, 8, "// line ") == 0) {
                            const Json point{{"line", row}, {"character", 0}};
                            result.push_back(
                                {{"range", {{"start", point}, {"end", point}}}, {"newText", " "}});
                        }
                        const auto end = text.find('\n', from);
                        if (end == text.npos)
                            break;
                        from = end + 1;
                    }
                } else if (mode == "format-crlf")
                    result[0]["newText"] = "\r\n  ";
                else if (mode == "format-tabs")
                    result[0]["newText"] = "\n\t";
                else if (mode == "format-unicode")
                    result[0]["newText"] = "/* café */ ";
                else if (mode == "format-disordered")
                    std::reverse(result.begin(), result.end());
            } else if (method == "textDocument/selectionRange") {
                const auto& parameters = request.at("params");
                if (parameters.contains("position") || parameters.at("positions").size() != 1)
                    throw std::runtime_error("Syntax query requires one positions entry");
                const auto uri = parameters.at("textDocument").at("uri").get<std::string>();
                const auto& text = drafts.at(uri);
                const auto offset =
                    forge::cpp_semantic_offset(text, parameters.at("positions").at(0));
                const auto [begin, end] = fixture_identifier(text, offset);
                const auto range = [&](std::size_t first, std::size_t last) {
                    return Json{{"start", forge::cpp_semantic_position(text, first)},
                                {"end", forge::cpp_semantic_position(text, last)}};
                };
                Json parent{{"range", range(0, text.size())}};
                if (mode == "selection-limited")
                    for (int i = 0; i != 17; ++i)
                        parent =
                            Json{{"range", range(0, text.size())}, {"parent", std::move(parent)}};
                result = Json::array({{{"range", range(begin, end)}, {"parent", parent}}});
                if (mode == "selection-null")
                    result = nullptr;
                else if (mode == "selection-empty")
                    result = Json::array();
                else if (mode == "selection-object")
                    result = result.at(0);
                else if (mode == "selection-node-null")
                    result = Json::array({nullptr});
                else if (mode == "selection-count")
                    result.push_back(result.at(0));
                else if (mode == "selection-parent")
                    result[0]["parent"]["range"] = range(0, 0);
                else if (mode == "selection-parent-type")
                    result[0]["parent"] = false;
                else if (mode == "selection-cursor")
                    result[0]["range"] = range(0, 0);
                else if (mode == "selection-reversed")
                    result[0]["range"] = range(end, begin);
                else if (mode == "selection-range")
                    result[0]["range"]["start"]["character"] = 999999;
                else if (mode == "selection-fractional")
                    result[0]["range"]["start"]["character"] = 1.5;
                else if (mode == "selection-split") {
                    result[0]["range"]["start"] =
                        forge::cpp_semantic_position(text, text.find("\xC3\xA9"));
                    result[0]["range"]["start"]["character"] =
                        result[0]["range"]["start"]["character"].get<std::size_t>() + 1;
                } else if (mode == "selection-duplicate")
                    result[0]["parent"] = Json{{"range", range(begin, end)}, {"parent", parent}};
            } else if (method == "textDocument/declaration" ||
                       method == "textDocument/references" ||
                       method == "textDocument/typeDefinition" ||
                       method == "textDocument/definition" ||
                       method == "textDocument/implementation") {
                if (method == "textDocument/references" &&
                    request.at("params").at("context").at("includeDeclaration") != false)
                    throw std::runtime_error("Reference query must exclude declarations");
                const auto uri =
                    request.at("params").at("textDocument").at("uri").get<std::string>();
                const auto& text = drafts.at(uri);
                const std::string marker =
                    method == "textDocument/definition" && text.find("navigation_body") != text.npos
                        ? "navigation_body"
                    : method == "textDocument/implementation" && text.find("override") != text.npos
                        ? "tick() override"
                    : method == "textDocument/typeDefinition"
                        ? (text.find("Pivot") != text.npos ? "Pivot" : "Spinner")
                    : text.find("outline_member") != text.npos ? "outline_member"
                                                               : "degrees_per_second";
                const auto begin =
                    method == "textDocument/references" || method == "textDocument/definition"
                        ? text.rfind(marker)
                        : text.find(marker);
                result = Json::array(
                    {{{"uri",
                       mode == "external"
                           ? forge::local_file_url(std::filesystem::current_path().parent_path() /
                                                   "Outside/Private.hpp")
                           : uri},
                      {"range",
                       {{"start", forge::cpp_semantic_position(text, begin)},
                        {"end", forge::cpp_semantic_position(text, begin + marker.size())}}}}});
                if (mode == "locations-null")
                    result = nullptr;
                else if (mode == "locations-empty")
                    result = Json::array();
                else if (mode == "locations-object")
                    result = result.at(0);
                else if (mode == "locations-range")
                    result[0]["range"]["start"]["character"] = 999999;
                else if (mode == "locations-fractional")
                    result[0]["range"]["start"]["character"] = 1.5;
                else if (mode == "locations-link" || mode == "locations-link-bounds") {
                    const auto item = result.at(0);
                    result = Json::array({{{"targetUri", item.at("uri")},
                                           {"targetRange", item.at("range")},
                                           {"targetSelectionRange", item.at("range")}}});
                    if (mode == "locations-link-bounds")
                        result[0]["targetRange"] = {{"start", {{"line", 0}, {"character", 0}}},
                                                    {"end", {{"line", 0}, {"character", 0}}}};
                } else if (mode == "locations-limited") {
                    const auto item = result.at(0);
                    for (int i = 1; i != 65; ++i)
                        result.push_back(item);
                }
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
