#pragma once
#include "ui_localization_authoring.hpp"
#include <chrono>

namespace test_ui_localization {
inline forge::AssetId
publish(const std::shared_ptr<forge::ProjectLease>& lease, const std::filesystem::path& path,
        std::function<forge::UiLocalizationSource(forge::AssetId)> initialize = {}) {
    auto document = forge::UiLocalizationDocument::create(lease, path, std::move(initialize));
    const auto id = document->source().asset();
    forge::AssetImportService imports(lease, forge::ui_localization_import_registry(),
                                      {"windows", "d3d12", "desktop"}, 1);
    imports.submit(
        imports.prepare(document->locator(), {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            forge::prepare_ui_localization_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    if (!imports.wait_idle(std::chrono::seconds(10)))
        throw std::runtime_error("Localization fixture publication timed out");
    const auto outcomes = imports.poll();
    if (outcomes.size() != 1 || !outcomes.front().published)
        throw std::runtime_error(outcomes.empty() ? "Missing localization publication receipt"
                                                  : outcomes.front().diagnostic);
    return id;
}
} // namespace test_ui_localization
