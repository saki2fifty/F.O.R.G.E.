#pragma once
#include <forge/assets.hpp>
#include <optional>
#include <string_view>
namespace forge::asset_storage {
// Private durable source/catalog IO shared by publication and asset file operations.
// Callers own ProjectPaths containment, writer lease and transaction semantics.
void ordinary(const std::filesystem::path&);
std::optional<std::string> read(const std::filesystem::path&,
                                std::size_t limit = max_asset_index_bytes);
void sync_directory(const std::filesystem::path&);
void replace(const std::filesystem::path&, std::string_view);
void erase_file(const std::filesystem::path&);
} // namespace forge::asset_storage
