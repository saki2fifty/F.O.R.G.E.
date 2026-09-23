#pragma once
#include <forge/ui_assets.hpp>
#include <stop_token>
namespace forge {
// One disposable native RmlUi process per document; no GPU or gameplay loading.
UiAssetSnapshot inspect_ui_dependencies(const std::filesystem::path& executable,
                                        const std::filesystem::path& font,
                                        const std::filesystem::path& project, AssetId,
                                        std::stop_token = {});
} // namespace forge
