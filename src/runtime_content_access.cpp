#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <forge/runtime_content_access.hpp>
namespace forge {
RuntimeContentAccess::RuntimeContentAccess(std::filesystem::path root) : paths_(std::move(root)) {
    const auto path = paths_.resolve("forge.runtime-content.json");
    if (!std::filesystem::exists(path))
        return;
    const auto bytes = asset_detail::read_bytes(path, 16 * 1024 * 1024);
    const auto doc = asset_detail::parse_bounded_json(bytes, 16 * 1024 * 1024, 1000000, 32);
    if (doc.at("format") != "forge.runtime-content" || doc.at("version") != 1 ||
        !doc.at("files").is_object() || doc.at("files").size() > 32768)
        throw std::runtime_error("package.manifest.invalid: Runtime content admission failed");
    files_ = doc.at("files");
}
std::vector<std::byte> RuntimeContentAccess::read(const std::filesystem::path& locator,
                                                  std::size_t limit) const {
    const auto normalized = ProjectPaths::normalize(locator);
    const auto key = path_utf8(normalized);
    if (files_ && !files_->contains(key))
        throw std::runtime_error("package.resource.undeclared: " + key);
    auto bytes = asset_detail::read_bytes(paths_.resolve(normalized), limit);
    if (files_) {
        const auto& entry = files_->at(key);
        if (!entry.at("bytes").is_number_unsigned() || entry.at("bytes") != bytes.size() ||
            entry.at("sha256") != asset_detail::content_digest(bytes))
            throw std::runtime_error("package.resource.corrupt: " + key);
    }
    return bytes;
}
} // namespace forge
