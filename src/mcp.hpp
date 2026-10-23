#pragma once
#include <atomic>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace forge {
// Tools-only adapter. The editor continues to own scene state and transactions.
int mcp_stdio();
nlohmann::json mcp_client_configuration(const std::filesystem::path& tools,
                                        const nlohmann::json& connection);
nlohmann::json mcp_editor_request(const nlohmann::json& connection, const nlohmann::json& request,
                                  const nlohmann::json& sequence,
                                  const std::atomic_bool& cancelled);
} // namespace forge
