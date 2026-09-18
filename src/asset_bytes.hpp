#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace forge::asset_detail {
std::vector<std::byte> read_bytes(const std::filesystem::path&, std::size_t limit);
std::string content_digest(std::span<const std::byte>);
} // namespace forge::asset_detail
