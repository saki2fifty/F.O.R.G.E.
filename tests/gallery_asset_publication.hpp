#pragma once
#include "asset_import_service.hpp"
#include <chrono>
#include <stdexcept>
#include <string>

namespace forge::test {
template <class Prepare>
void publish_gallery_asset(AssetImportService& service, const std::filesystem::path& source,
                           Prepare prepare,
                           std::chrono::milliseconds timeout = std::chrono::seconds(90)) {
    service.submit(
        service.prepare(source),
        [&](auto& candidate, const auto& plan, const auto&) { prepare(candidate, plan); },
        [](const auto&, const auto&) {});
    if (!service.wait_idle(timeout))
        throw std::runtime_error("Gallery publication timed out: " + source.generic_string());
    const auto results = service.poll();
    if (results.size() != 1 || !results[0].published)
        throw std::runtime_error(
            "Gallery publication failed: " + source.generic_string() + ": " +
            (results.empty() ? std::string("missing result") : results[0].diagnostic));
}
} // namespace forge::test
