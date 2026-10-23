#include "mcp.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <forge/build.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr auto current_version = "2026-07-28";
constexpr auto legacy_version = "2025-11-25";
constexpr auto instructions =
    "FORGE edits the scene open in the owning editor. Start with forge_discover. Carry its "
    "complete target into document calls. Mutations require your observed expected_revision and "
    "next_sequence as sequence; repeating identical arguments with the same sequence retrieves "
    "the editor receipt. Never blindly retry with a new sequence or silently replace a stale "
    "revision. Scene edits share Undo and remain unsaved until saved in the editor. No project "
    "file, asset, C++ compilation, Play, export or engine-source tools are available yet.";

Json object(Json properties = Json::object(), Json required = Json::array()) {
    if (properties.is_null())
        properties = Json::object();
    if (required.is_null())
        required = Json::array();
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}
Json number(std::uint64_t minimum = 0) {
    return {{"type", "integer"}, {"minimum", minimum}, {"maximum", 9007199254740991ULL}};
}
Json error(const Json& id, int code, const char* message, Json data = nullptr) {
    Json detail = {{"code", code}, {"message", message}};
    if (!data.is_null())
        detail["data"] = std::move(data);
    Json response = {{"jsonrpc", "2.0"}, {"error", std::move(detail)}};
    // Both pinned MCP schemas omit an unavailable correlation ID rather than use null.
    if (!id.is_null())
        response["id"] = id;
    return response;
}
bool valid_id(const Json& value) { return value.is_string() || value.is_number_integer(); }
Json tool_result(Json response) {
    return {{"content", Json::array({{{"type", "text"}, {"text", response.dump()}}})},
            {"structuredContent", response},
            {"isError", !response.value("ok", false)}};
}
Json tool_failure(const char* code, const char* message) {
    return tool_result({{"ok", false}, {"error", {{"code", code}, {"message", message}}}});
}
std::string command_name(std::string operation) {
    std::replace(operation.begin(), operation.end(), '.', '_');
    return "forge_" + operation;
}
struct Tool {
    Json description;
    std::string method, operation;
    bool mutation = false;
};
std::vector<Tool> tools(const Json& discovery) {
    std::vector<Tool> result;
    const auto target = object({{"kind", {{"const", "scene"}, {"type", "string"}}},
                                {"id", {{"type", "string"}}},
                                {"session", {{"type", "string"}}}},
                               {"kind", "id", "session"});
    auto add = [&](std::string name, const char* description, std::string method, Json properties,
                   Json required, bool edit = false, std::string operation = {}) {
        if (std::any_of(result.begin(), result.end(),
                        [&](const Tool& item) { return item.description.at("name") == name; }))
            throw std::runtime_error("Shared authoring catalog has conflicting MCP tool names");
        if (method != "discover") {
            properties["target"] = target;
            required.push_back("target");
        }
        if (edit) {
            properties["expected_revision"] = number();
            properties["sequence"] = number(1);
            required.push_back("expected_revision");
            required.push_back("sequence");
        }
        result.push_back({{{"name", std::move(name)},
                           {"description", description},
                           {"inputSchema", object(std::move(properties), std::move(required))},
                           {"annotations",
                            {{"readOnlyHint", !edit},
                             {"destructiveHint", edit},
                             {"idempotentHint", true},
                             {"openWorldHint", false}}}},
                          std::move(method),
                          std::move(operation),
                          edit});
    };
    add("forge_discover",
        "Read current target, revision, next_sequence, component schema, "
        "command schemas and connection permissions. No secrets are returned.",
        "discover", {}, {});
    add("forge_scene_read", "Read the committed authored scene, including entity IDs and values.",
        "scene.read", {}, {});
    add("forge_entity_query", "Page effective entity/component values; text is case-sensitive.",
        "entity.query",
        {{"text", {{"type", "string"}}},
         {"component", {{"type", "string"}}},
         {"offset", number()},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 256}}}},
        {});
    add("forge_scene_diagnostics", "Read scene counts and authoring diagnostics.",
        "scene.diagnostics", {}, {});
    const auto& detail = discovery.at("result");
    const auto& methods = detail.at("methods");
    auto supports = [&](const char* method) {
        return std::find(methods.begin(), methods.end(), method) != methods.end();
    };
    if (supports("scene.apply")) {
        Json variants = Json::array();
        for (const auto& command : detail.at("commands")) {
            const auto operation = command.at("id").get<std::string>();
            const auto args = command.at("input_schema");
            add(command_name(operation),
                command.at("description").get_ref<const std::string&>().c_str(), "scene.apply",
                {{"arguments", args}}, {"arguments"}, true, operation);
            variants.push_back(object(
                {{"operation", {{"type", "string"}, {"const", operation}}}, {"arguments", args}},
                {"operation", "arguments"}));
        }
        add("forge_scene_apply",
            "Apply 1..128 commands atomically as one Undo entry. A failure "
            "rejects the complete batch. Use discover for component fields.",
            "scene.apply",
            {{"commands",
              {{"type", "array"},
               {"minItems", 1},
               {"maxItems", 128},
               {"items", {{"oneOf", variants}}}}}},
            {"commands"}, true);
    }
    if (supports("scene.replace"))
        add("forge_scene_replace",
            "Replace authored scene intent as one validated Undo entry; "
            "this does not open or save files.",
            "scene.replace", {{"document", {{"type", "object"}}}}, {"document"}, true);
    if (supports("history.undo"))
        add("forge_history_undo", "Undo one authored scene transaction.", "history.undo", {}, {},
            true);
    if (supports("history.redo"))
        add("forge_history_redo", "Redo one authored scene transaction.", "history.redo", {}, {},
            true);
    return result;
}

class Protocol {
  public:
    explicit Protocol(Json connection) : connection_(std::move(connection)) {}
    std::optional<Json> handle(const Json& request, const std::atomic_bool& cancelled) {
        const auto id = request.is_object() && request.contains("id") && valid_id(request["id"])
                            ? request["id"]
                            : Json(nullptr);
        if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" ||
            !request.contains("method") || !request["method"].is_string() ||
            (request.contains("id") && id.is_null()) ||
            (request.contains("params") && !request["params"].is_object()))
            return error(id, -32600, "Invalid JSON-RPC request");
        const auto method = request["method"].get<std::string>();
        const auto params = request.value("params", Json::object());
        // Notifications never invoke tools or other scene operations.
        if (!request.contains("id")) {
            if (method == "notifications/initialized" && initializing_)
                initialized_ = true;
            return std::nullopt;
        }
        bool modern = false;
        if (params.contains("_meta")) {
            const auto& meta = params["_meta"];
            if (!meta.is_object())
                return error(id, -32602, "Request metadata must be an object");
            if (meta.contains("io.modelcontextprotocol/protocolVersion")) {
                modern = true;
                const auto version = meta["io.modelcontextprotocol/protocolVersion"];
                if (!version.is_string())
                    return error(id, -32602, "Protocol version must be a string");
                if (version != current_version)
                    return error(id, -32022, "Unsupported protocol version",
                                 {{"requested", version}, {"supported", {current_version}}});
                if (!meta.contains("io.modelcontextprotocol/clientCapabilities") ||
                    !meta["io.modelcontextprotocol/clientCapabilities"].is_object())
                    return error(id, -32602, "Per-request client capabilities are required");
            }
        }
        Json result;
        if (!modern && method == "initialize") {
            if (initializing_ || !params.contains("protocolVersion") ||
                !params["protocolVersion"].is_string() || !params.contains("capabilities") ||
                !params["capabilities"].is_object() || !params.contains("clientInfo") ||
                !params["clientInfo"].is_object() || !params["clientInfo"].contains("name") ||
                !params["clientInfo"]["name"].is_string() ||
                !params["clientInfo"].contains("version") ||
                !params["clientInfo"]["version"].is_string())
                return error(id, -32602, "Invalid or repeated initialization");
            initializing_ = true;
            result = {{"protocolVersion", legacy_version},
                      {"capabilities", {{"tools", Json::object()}}},
                      {"serverInfo", server_info()},
                      {"instructions", instructions}};
        } else if (!modern && method == "ping") {
            result = Json::object();
        } else {
            if (!modern && !initialized_)
                return error(id, -32002, "Initialize MCP or send current per-request metadata");
            if (modern && method == "server/discover") {
                result = {{"supportedVersions", {current_version}},
                          {"capabilities", {{"tools", Json::object()}}},
                          {"instructions", instructions},
                          {"ttlMs", 0},
                          {"cacheScope", "private"}};
            } else if (method == "tools/list" || method == "tools/call") {
                try {
                    const auto discovery = mcp_editor_request(
                        connection_, {{"api", 1}, {"method", "discover"}}, nullptr, cancelled);
                    if (!discovery.value("ok", false)) {
                        if (method == "tools/call")
                            result = tool_result(discovery);
                        else
                            return error(id, -32000, "Editor connection denied or unavailable");
                    } else {
                        const auto catalog = tools(discovery);
                        if (method == "tools/list") {
                            if (params.contains("cursor"))
                                return error(id, -32602,
                                             "This tool catalog has no continuation cursor");
                            result = {{"tools", Json::array()}};
                            for (const auto& tool : catalog)
                                result["tools"].push_back(tool.description);
                            if (modern) {
                                result["ttlMs"] = 0;
                                result["cacheScope"] = "private";
                            }
                        } else {
                            if (!params.contains("name") || !params["name"].is_string() ||
                                (params.contains("arguments") && !params["arguments"].is_object()))
                                return error(id, -32602, "Expected tool name and object arguments");
                            const auto found =
                                std::find_if(catalog.begin(), catalog.end(), [&](const Tool& item) {
                                    return item.description["name"] == params["name"];
                                });
                            if (found == catalog.end())
                                return error(id, -32602,
                                             "Tool is not available on this connection");
                            const auto args = params.value("arguments", Json::object());
                            const auto& schema = found->description["inputSchema"];
                            bool valid = true;
                            for (const auto& field : schema["required"])
                                valid = valid && args.contains(field.get<std::string>());
                            for (auto it = args.begin(); it != args.end(); ++it)
                                valid = valid && schema["properties"].contains(it.key());
                            if (found->mutation)
                                for (const auto* key : {"sequence", "expected_revision"})
                                    valid = valid && args.contains(key) &&
                                            args[key].is_number_unsigned() &&
                                            args[key].get<std::uint64_t>() <= 9007199254740991ULL &&
                                            (std::string(key) != "sequence" || args[key] != 0);
                            if (!valid)
                                result = tool_failure("invalid_arguments",
                                                      "Missing, unknown or invalid tool arguments");
                            else if (found->method == "discover")
                                result = tool_result(discovery);
                            else {
                                auto authored = args;
                                authored.erase("sequence");
                                if (!found->operation.empty()) {
                                    authored.erase("arguments");
                                    authored["commands"] =
                                        Json::array({{{"operation", found->operation},
                                                      {"arguments", args.at("arguments")}}});
                                }
                                authored["api"] = 1;
                                authored["method"] = found->method;
                                result = tool_result(
                                    mcp_editor_request(connection_, authored,
                                                       args.value("sequence", Json()), cancelled));
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (method == "tools/list")
                        return error(id, -32000,
                                     "Editor unavailable; start a new local connection if needed");
                    result = tool_failure("connection_unavailable", e.what());
                }
            } else
                return error(id, -32601, "Method not supported");
        }
        if (modern) {
            result["resultType"] = "complete";
            result["_meta"] = {{"io.modelcontextprotocol/serverInfo", server_info()}};
        }
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    }

  private:
    static Json server_info() { return {{"name", "forge"}, {"version", build_id}}; }
    Json connection_;
    bool initializing_ = false, initialized_ = false;
};
} // namespace

nlohmann::json mcp_client_configuration(const std::filesystem::path& tools,
                                        const Json& connection) {
    const auto path = tools.u8string();
    return {{"mcpServers",
             {{"forge",
               {{"command", std::string(path.begin(), path.end())},
                {"args", {"--mcp-stdio"}},
                {"env", {{"FORGE_AUTOMATION_CONNECTION", connection.dump()}}}}}}}};
}

int mcp_stdio() {
    Json connection;
    try {
        const char* value = std::getenv("FORGE_AUTOMATION_CONNECTION");
        if (!value || std::char_traits<char>::length(value) > 8192)
            throw std::runtime_error("Missing connection");
        connection = Json::parse(value, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 8)
                throw std::runtime_error("Invalid connection nesting");
            return true;
        });
        if (connection.at("transport") != 1 || connection.at("host") != "127.0.0.1" ||
            !connection.at("port").is_number_unsigned() ||
            connection.at("port").get<std::uint64_t>() < 1 ||
            connection.at("port").get<std::uint64_t>() > 65535 ||
            !connection.at("token").is_string())
            throw std::runtime_error("Invalid connection");
        const auto token = connection.at("token").get<std::string>();
        if (token.size() != 64 || token.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Invalid token");
    } catch (const std::exception&) {
        std::cerr << "FORGE MCP needs valid FORGE_AUTOMATION_CONNECTION from the editor's "
                     "Copy MCP configuration action. Connection contents are not logged.\n";
        return 2;
    }
    Protocol protocol(std::move(connection));
    // The reader must not implicitly flush the dispatch thread's output per input byte.
    std::cin.tie(nullptr);
    struct Pending {
        Json wire;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        std::string key;
        bool parse_error = false;
    };
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Pending> queue;
    std::map<std::string, std::shared_ptr<std::atomic_bool>> active;
    bool closed = false;
    // One bounded reader permits cancellation while the editor connection is waiting.
    // The single dispatch thread preserves legacy MCP ordering and editor transactions.
    std::thread reader([&] {
        while (std::cin) {
            std::string line;
            char ch;
            bool overflow = false;
            while (std::cin.get(ch) && ch != '\n') {
                if (line.size() < 1048576)
                    line.push_back(ch);
                else
                    overflow = true;
            }
            if (!std::cin)
                break;
            Pending pending;
            try {
                if (overflow)
                    throw std::runtime_error("Input too large");
                pending.wire = Json::parse(line, [](int depth, Json::parse_event_t, Json&) {
                    if (depth > 64)
                        throw std::runtime_error("Input too deep");
                    return true;
                });
            } catch (const std::exception&) {
                pending.parse_error = true;
            }
            std::lock_guard lock(mutex);
            if (!pending.parse_error && pending.wire.is_object()) {
                const auto& wire = pending.wire;
                if (!wire.contains("id") && wire.value("jsonrpc", Json()) == "2.0" &&
                    wire.value("method", Json()) == "notifications/cancelled") {
                    if (wire.contains("params") && wire["params"].is_object() &&
                        wire["params"].contains("requestId") &&
                        valid_id(wire["params"]["requestId"])) {
                        const auto found = active.find(wire["params"]["requestId"].dump());
                        if (found != active.end())
                            *found->second = true;
                    }
                    continue;
                }
                if (wire.contains("id") && valid_id(wire["id"])) {
                    pending.key = wire["id"].dump();
                    if (active.contains(pending.key))
                        break;
                    active[pending.key] = pending.cancelled;
                }
            }
            // Abusive pipelining closes the bridge; it never allocates an unbounded queue.
            if (queue.size() >= 8)
                break;
            queue.push_back(std::move(pending));
            ready.notify_one();
        }
        std::lock_guard lock(mutex);
        closed = true;
        for (auto& [key, flag] : active)
            *flag = true;
        ready.notify_one();
    });
    while (true) {
        Pending pending;
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, [&] { return closed || !queue.empty(); });
            if (closed)
                break;
            pending = std::move(queue.front());
            queue.pop_front();
        }
        std::optional<Json> response;
        if (!*pending.cancelled) {
            try {
                if (pending.parse_error)
                    response = error(nullptr, -32700, "Invalid, oversized or deeply nested JSON");
                else
                    response = protocol.handle(pending.wire, *pending.cancelled);
            } catch (const std::exception&) {
                const auto id = pending.wire.is_object() && pending.wire.contains("id") &&
                                        valid_id(pending.wire["id"])
                                    ? pending.wire["id"]
                                    : Json();
                response = error(id, -32603, "MCP request failed");
            }
        }
        std::lock_guard lock(mutex);
        active.erase(pending.key);
        if (response && !*pending.cancelled) {
            auto wire = response->dump();
            if (wire.size() > 34 * 1024 * 1024)
                wire = error(response->value("id", Json()), -32000,
                             "MCP response too large; use paged queries")
                           .dump();
            std::cout << wire << std::endl;
        }
    }
    reader.join();
    return 0;
}
} // namespace forge
