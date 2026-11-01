#pragma once
#include <cstddef>
#include <forge/ui_components.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
inline constexpr std::size_t ui_localization_byte_limit = 256 * 1024;
// Bounded ASCII language tags, compared without case. No platform locale state.
std::string canonical_ui_locale(std::string_view);
bool ui_locale_tag(std::string_view) noexcept;
bool ui_localization_key(std::string_view);
struct UiLocalizedMessage {
    std::string text, locale;
    bool fallback = false, missing = false;
};
struct UiLocalizationLocale {
    std::string label;
    std::map<std::string, std::string> messages;
};
struct UiLocalizationData {
    AssetId asset;
    std::string default_locale;
    std::map<std::string, UiLocalizationLocale> locales;
    std::string select_locale(std::string_view requested) const;
    UiLocalizedMessage message(std::string_view key, std::string_view requested) const;
    std::size_t resident_bytes() const;
};
struct UiLocalizationSource {
    nlohmann::json document;
    static UiLocalizationSource create(AssetId);
    static UiLocalizationSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    UiLocalizationData compiled() const;
    std::vector<std::byte> cook() const;
};
} // namespace forge
