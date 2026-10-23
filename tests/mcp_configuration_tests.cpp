#include "mcp.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto tools = std::filesystem::u8path("FORGE folder/\xc3\xa9/bin/forge_tools.exe");
        const auto path = tools.u8string();
        const auto token = std::string(64, 'a');
        const nlohmann::json connection = {
            {"transport", 1}, {"host", "127.0.0.1"}, {"port", 12345}, {"token", token}};
        const auto config = forge::mcp_client_configuration(tools, connection);
        const auto& server = config.at("mcpServers").at("forge");
        if (server.at("command") != std::string(path.begin(), path.end()) ||
            server.at("args") != nlohmann::json::array({"--mcp-stdio"}) ||
            nlohmann::json::parse(
                server.at("env").at("FORGE_AUTOMATION_CONNECTION").get<std::string>()) !=
                connection ||
            server.at("args").dump().find(token) != std::string::npos)
            throw std::runtime_error("MCP client configuration changed path or credential scope");
        auto changed = connection;
        changed["token"] = std::string(64, 'b');
        const auto rotated = forge::mcp_client_configuration(tools, changed).dump();
        if (rotated.find(token) != std::string::npos)
            throw std::runtime_error("MCP configuration retained an old connection token");
        std::cout << "MCP configuration preserves UTF-8 paths and uses the current environment "
                     "credential\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
