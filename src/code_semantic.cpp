#include "code_semantic.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include "editor/cpp_source_search.hpp"
#include "local_file_url.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <iostream>
#include <map>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t frame_limit = 8 * 1024 * 1024, input_limit = 64 * 1024 * 1024;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool boundary(std::string_view text, std::size_t offset) {
    return offset <= text.size() &&
           (offset == text.size() || (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
}
std::string semantic_string(const Json& value, std::size_t limit = 4096) {
    auto text = value.get<std::string>();
    require(text.size() <= limit, "C++ semantic string exceeds its limit");
    ui::validate_cpp_text(text);
    return text;
}
Json normalized_completions(const Json& response, const std::string& source) {
    const auto items = response.is_null()    ? Json::array()
                       : response.is_array() ? response
                                             : response.at("items");
    require(items.is_array(), "C++ semantic completion reply needs an item list");
    Json output = Json::array();
    for (const auto& item : items) {
        if (output.size() == 64)
            break;
        Json row{{"label", semantic_string(item.at("label"))},
                 {"detail", semantic_string(item.value("detail", Json("")))},
                 {"available", false},
                 {"reason", "Completion needs an unsupported edit"}};
        if (item.contains("command") || !item.contains("textEdit") ||
            item.value("insertTextFormat", 1) != 1 ||
            (item.contains("additionalTextEdits") && !item.at("additionalTextEdits").empty())) {
            output.push_back(std::move(row));
            continue;
        }
        const auto& edit = item.at("textEdit");
        if (!edit.contains("range")) {
            output.push_back(std::move(row));
            continue;
        }
        const auto begin = cpp_semantic_offset(source, edit.at("range").at("start"));
        const auto end = cpp_semantic_offset(source, edit.at("range").at("end"));
        require(begin <= end, "Reversed C++ semantic replacement range");
        auto replacement = semantic_string(edit.at("newText"), 16384);
        auto candidate = source;
        candidate.replace(begin, end - begin, replacement);
        ui::validate_cpp_text(candidate);
        row.update({{"available", true},
                    {"reason", ""},
                    {"begin", begin},
                    {"end", end},
                    {"text", std::move(replacement)}});
        output.push_back(std::move(row));
    }
    return {{"items", std::move(output)},
            {"limited",
             items.size() > 64 || (response.is_object() && response.value("isIncomplete", false))}};
}
Json normalized_declarations(const Json& response, const ProjectPaths& paths,
                             const ui::CppSearchDrafts& drafts) {
    const auto locations = response.is_null()    ? Json::array()
                           : response.is_array() ? response
                                                 : Json::array({response});
    Json output = Json::array();
    for (const auto& location : locations) {
        if (output.size() == 64)
            break;
        const bool link = location.contains("targetUri");
        const auto path =
            local_file_url_path(semantic_string(location.at(link ? "targetUri" : "uri"), 8192));
        std::filesystem::path locator;
        try {
            locator = paths.relative(path);
            (void)ui::cpp_source_path(paths.root(), locator);
        } catch (const std::exception&) {
            output.push_back({{"available", false},
                              {"file", path_utf8(path.filename())},
                              {"reason", "Declaration is outside editable Native sources"}});
            continue;
        }
        const auto draft = drafts.find(locator);
        const auto text = draft != drafts.end() ? draft->second : ui::read_cpp_source(path);
        const auto& range = location.at(link ? "targetSelectionRange" : "range");
        const auto begin = cpp_semantic_offset(text, range.at("start"));
        const auto end = cpp_semantic_offset(text, range.at("end"));
        require(begin <= end, "Reversed C++ semantic declaration range");
        const auto [row, column] = ui::cpp_source_coordinate(text, begin);
        output.push_back({{"available", true},
                          {"reason", ""},
                          {"path", path_utf8(locator)},
                          {"state", ui::cpp_source_state(text)},
                          {"begin", begin},
                          {"end", end},
                          {"line", row + 1},
                          {"column", column + 1}});
    }
    return {{"items", std::move(output)}, {"limited", locations.size() > 64}};
}
class SemanticTransport {
    SDL_Process* process_ = nullptr;
    SDL_IOStream *input_ = nullptr, *output_ = nullptr;
    Uint64 deadline_ = SDL_GetTicks() + 30000;
    std::int64_t identifier_ = 0;
    void alive() const { require(SDL_GetTicks() < deadline_, "C++ semantic query timed out"); }
    void transfer(char* data, std::size_t size, bool writing) {
        while (size) {
            alive();
            auto* stream = writing ? input_ : output_;
            const auto count =
                writing ? SDL_WriteIO(stream, data, size) : SDL_ReadIO(stream, data, size);
            data += count;
            size -= count;
            if (!count) {
                require(SDL_GetIOStatus(stream) == SDL_IO_STATUS_NOT_READY,
                        "C++ semantic tool closed or failed its protocol pipe");
                SDL_Delay(1);
            }
        }
    }
    Json receive() {
        std::string header;
        while (!header.ends_with("\r\n\r\n")) {
            require(header.size() < 8192, "C++ semantic reply header exceeds its limit");
            char byte = 0;
            transfer(&byte, 1, false);
            header += byte;
        }
        std::optional<std::size_t> length;
        for (std::size_t start = 0, end; (end = header.find("\r\n", start)) != header.npos;
             start = end + 2) {
            const std::string_view line(header.data() + start, end - start);
            constexpr std::string_view key = "content-length:";
            if (line.size() < key.size() ||
                !std::equal(key.begin(), key.end(), line.begin(), [](char a, char b) {
                    return a == (b >= 'A' && b <= 'Z' ? b + ('a' - 'A') : b);
                }))
                continue;
            require(!length, "Duplicate C++ semantic reply length");
            auto value = line.substr(15);
            while (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);
            std::size_t count = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                        count && count <= frame_limit,
                    "Invalid or oversized C++ semantic reply length");
            length = count;
        }
        require(length.has_value(), "C++ semantic reply has no length");
        std::string body(*length, '\0');
        transfer(body.data(), body.size(), false);
        return asset_detail::parse_bounded_json(std::as_bytes(std::span(body.data(), body.size())),
                                                frame_limit);
    }

  public:
    SemanticTransport(const SemanticTransport&) = delete;
    SemanticTransport& operator=(const SemanticTransport&) = delete;
    SemanticTransport(const std::string& tool, const std::filesystem::path& project,
                      const std::filesystem::path& context) {
        require(!tool.empty() && tool.size() <= 8192 && tool.find('\0') == tool.npos,
                "Choose a clangd 23.1.0 executable");
        const auto directory = path_utf8(project);
        const auto database = "--compile-commands-dir=" + path_utf8(context);
        const char* args[]{tool.c_str(),
                           "--enable-config=false",
                           "--background-index=false",
                           "--clang-tidy=false",
                           "--header-insertion=never",
                           "--pch-storage=memory",
                           "--use-dirty-headers",
                           "--log=error",
                           "-j=2",
                           "--limit-results=64",
                           database.c_str(),
                           nullptr};
        const auto properties = SDL_CreateProperties();
        require(properties != 0, "Cannot create semantic tool process properties");
        const bool configured =
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args) &&
            SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                                  directory.c_str()) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                                  SDL_PROCESS_STDIO_INHERITED);
        if (configured)
            process_ = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        require(process_ != nullptr, "Cannot start clangd 23.1.0; check the configured executable");
        const auto streams = SDL_GetProcessProperties(process_);
        input_ = static_cast<SDL_IOStream*>(
            SDL_GetPointerProperty(streams, SDL_PROP_PROCESS_STDIN_POINTER, nullptr));
        output_ = static_cast<SDL_IOStream*>(
            SDL_GetPointerProperty(streams, SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
        if (!input_ || !output_) {
            SDL_KillProcess(process_, true);
            SDL_WaitProcess(process_, true, nullptr);
            SDL_DestroyProcess(process_);
            process_ = nullptr;
            throw std::runtime_error("Cannot open the semantic tool protocol pipes");
        }
    }
    ~SemanticTransport() {
        if (process_) {
            int code = 0;
            if (!SDL_WaitProcess(process_, false, &code)) {
                SDL_KillProcess(process_, true);
                SDL_WaitProcess(process_, true, &code);
            }
            SDL_DestroyProcess(process_);
        }
    }
    void send(Json message) {
        message["jsonrpc"] = "2.0";
        auto body = message.dump();
        require(body.size() <= frame_limit, "C++ semantic request exceeds its frame limit");
        auto header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        transfer(header.data(), header.size(), true);
        transfer(body.data(), body.size(), true);
        require(SDL_FlushIO(input_), "Cannot flush C++ semantic request");
    }
    Json call(const std::string& method, Json params) {
        const auto id = ++identifier_;
        send({{"id", id}, {"method", method}, {"params", std::move(params)}});
        for (unsigned i = 0; i < 512; ++i) {
            auto message = receive();
            require(message.is_object() && message.value("jsonrpc", "") == "2.0",
                    "Invalid C++ semantic response envelope");
            if (message.contains("method")) {
                if (message.contains("id"))
                    send({{"id", message.at("id")},
                          {"error",
                           {{"code", -32601}, {"message", "Client commands are disabled"}}}});
                continue;
            }
            require(message.contains("id") && message.at("id").get<std::int64_t>() == id,
                    "C++ semantic response does not match the current query");
            require(message.contains("result") && !message.contains("error"),
                    "C++ semantic tool refused the query");
            return message.at("result");
        }
        throw std::runtime_error("C++ semantic notification budget exhausted");
    }
    void finish() {
        call("shutdown", nullptr);
        send({{"method", "exit"}, {"params", nullptr}});
        SDL_CloseIO(input_);
        input_ = nullptr;
        int code = -1;
        while (!SDL_WaitProcess(process_, false, &code)) {
            alive();
            SDL_Delay(1);
        }
        require(code == 0, "C++ semantic tool did not exit cleanly");
    }
};
} // namespace

nlohmann::json cpp_semantic_position(std::string_view text, std::size_t offset) {
    require(boundary(text, offset), "Semantic cursor splits a UTF-8 character");
    std::size_t row = 0, start = 0;
    for (std::size_t i = 0; i < offset; ++i)
        if (text[i] == '\n') {
            ++row;
            start = i + 1;
        }
    require(!offset || offset == text.size() || text[offset] != '\n' || text[offset - 1] != '\r',
            "Semantic cursor splits a CRLF line ending");
    return {{"line", row}, {"character", offset - start}};
}
std::size_t cpp_semantic_offset(std::string_view text, const nlohmann::json& position) {
    require(position.is_object() && position.at("line").is_number_integer() &&
                position.at("character").is_number_integer(),
            "Invalid C++ semantic source position");
    const auto row = position.at("line").get<std::int64_t>();
    const auto column = position.at("character").get<std::int64_t>();
    require(row >= 0 && column >= 0 && std::uint64_t(row) <= text.size() &&
                std::uint64_t(column) <= text.size(),
            "C++ semantic position is outside its source");
    std::size_t start = 0;
    for (std::int64_t i = 0; i < row; ++i) {
        const auto end = text.find('\n', start);
        require(end != text.npos, "C++ semantic line is outside its source");
        start = end + 1;
    }
    auto end = text.find('\n', start);
    if (end == text.npos)
        end = text.size();
    if (end > start && text[end - 1] == '\r')
        --end;
    require(std::uint64_t(column) <= end - start && boundary(text, start + column),
            "C++ semantic column is outside its line or splits UTF-8");
    return start + column;
}

nlohmann::json query_cpp_semantics(const nlohmann::json& request) {
    require(request.is_object() && request.at("version").get<std::int64_t>() == 1,
            "Unsupported C++ semantic query version");
    const auto kind = request.at("kind").get<std::string>();
    require(kind == "completion" || kind == "declaration", "Choose completion or declaration");
    const ProjectPaths paths(std::filesystem::u8path(request.at("project").get<std::string>()));
    const auto context = paths.resolve(
        paths.relative(std::filesystem::u8path(request.at("context").get<std::string>())));
    require(std::filesystem::is_regular_file(context / "compile_commands.json"),
            "Build C++ once to generate this project's compiler context");
    const auto database = asset_storage::read(context / "compile_commands.json", 16 * 1024 * 1024);
    require(database.has_value(), "C++ compiler context is unavailable");
    const auto context_state = ui::cpp_source_state(*database);
    ui::CppSearchDrafts documents;
    std::size_t bytes = 0;
    require(request.at("documents").is_array() && request.at("documents").size() <= 64,
            "C++ semantic queries support at most 64 open documents");
    for (const auto& row : request.at("documents")) {
        const auto locator =
            ProjectPaths::normalize(std::filesystem::u8path(row.at("path").get<std::string>()));
        (void)ui::cpp_source_path(paths.root(), locator);
        auto text = row.at("text").get<std::string>();
        ui::validate_cpp_text(text);
        require(text.size() <= ui::cpp_search_byte_limit - bytes,
                "C++ semantic drafts exceed 32 MiB");
        bytes += text.size();
        require(documents.emplace(locator, std::move(text)).second,
                "Duplicate C++ semantic source locator");
    }
    const auto target =
        ProjectPaths::normalize(std::filesystem::u8path(request.at("path").get<std::string>()));
    (void)ui::cpp_source_path(paths.root(), target);
    const auto found = documents.find(target);
    require(found != documents.end(), "Open the C++ source before requesting semantic information");
    const auto& text = found->second;
    require(request.at("offset").is_number_integer() &&
                request.at("offset").get<std::int64_t>() >= 0,
            "Use a nonnegative source byte offset");
    const auto position = cpp_semantic_position(text, request.at("offset").get<std::size_t>());
    SemanticTransport transport(request.at("tool").get<std::string>(), paths.root(), context);
    const auto initialized = transport.call(
        "initialize", {{"processId", nullptr},
                       {"rootUri", local_file_url(paths.root())},
                       {"capabilities",
                        {{"general", {{"positionEncodings", {"utf-8"}}}},
                         {"textDocument",
                          {{"completion",
                            {{"completionItem",
                              {{"snippetSupport", false}, {"insertReplaceSupport", false}}}}}}}}}});
    const auto version = semantic_string(initialized.at("serverInfo").at("version"));
    require(initialized.at("serverInfo").at("name").get<std::string>() == "clangd" &&
                version.starts_with("clangd version 23.1.0 (") &&
                version.find("ea7d852a70e8bdfaf601d6626a760f9771b2c4b4") != version.npos &&
                initialized.at("capabilities").at("positionEncoding").get<std::string>() == "utf-8",
            "Semantic tools require the supported clangd 23.1.0 release and UTF-8 positions");
    require(initialized.at("capabilities").at("documentSymbolProvider").get<bool>(),
            "Semantic tools require document-symbol readiness support");
    transport.send({{"method", "initialized"}, {"params", Json::object()}});
    const auto open_document = [&](const auto& path, const auto& draft) {
        transport.send({{"method", "textDocument/didOpen"},
                        {"params",
                         {{"textDocument",
                           {{"uri", local_file_url(paths.resolve(path))},
                            {"languageId", "cpp"},
                            {"version", 1},
                            {"text", draft}}}}}});
    };
    // Supply dependencies before the target starts parsing. Wait for its initial
    // AST through a read-only request: initial completion may otherwise use a
    // fallback before the current unsaved headers have been parsed.
    for (const auto& [path, draft] : documents)
        if (!paths.same_locator(path, target))
            open_document(path, draft);
    open_document(target, text);
    (void)transport.call("textDocument/documentSymbol",
                         {{"textDocument", {{"uri", local_file_url(paths.resolve(target))}}}});
    auto response = transport.call(
        "textDocument/" + kind, {{"textDocument", {{"uri", local_file_url(paths.resolve(target))}}},
                                 {"position", position}});
    transport.finish();
    const auto current_database =
        asset_storage::read(context / "compile_commands.json", 16 * 1024 * 1024);
    require(current_database && ui::cpp_source_state(*current_database) == context_state,
            "C++ compiler context changed during analysis; query again");
    auto result = kind == "completion" ? normalized_completions(response, text)
                                       : normalized_declarations(response, paths, documents);
    result.update({{"version", 1},
                   {"id", request.at("id")},
                   {"kind", kind},
                   {"path", path_utf8(target)},
                   {"state", ui::cpp_source_state(text)},
                   {"context_state", context_state},
                   {"tool_version", version}});
    return result;
}
int cpp_semantic_worker(const std::filesystem::path& input, const std::filesystem::path& output) {
    try {
        const auto text = asset_storage::read(input, input_limit);
        require(text.has_value(), "Semantic request no longer exists");
        const auto request = asset_detail::parse_bounded_json(
            std::as_bytes(std::span(text->data(), text->size())), input_limit);
        const ProjectPaths paths(std::filesystem::u8path(request.at("project").get<std::string>()));
        const auto id = AssetId::parse(request.at("id").get<std::string>()).str();
        const auto folder = std::filesystem::path(".forge/native/semantic") / id;
        require(paths.same_locator(paths.relative(input), folder / "request.json") &&
                    paths.same_locator(paths.relative(output), folder / "response.json"),
                "Semantic worker IO must remain in its private project job folder");
        asset_storage::ordinary(input);
        asset_storage::ordinary(output);
        require(SDL_Init(0), "Cannot initialize semantic process support");
        struct Shutdown {
            ~Shutdown() { SDL_Quit(); }
        } shutdown;
        const auto result = query_cpp_semantics(request);
        asset_storage::replace(output, result.dump());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "C++ semantic query failed: " << error.what() << '\n';
        return 1;
    }
}
} // namespace forge
