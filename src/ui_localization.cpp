#include "bounded_json.hpp"
#include <algorithm>
#include <forge/ui_localization.hpp>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool letter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
const std::string& text(const Json& value, std::size_t maximum, bool nonempty) {
    require(value.is_string(), "Localization values must be UTF-8 strings");
    const auto& s = value.get_ref<const std::string&>();
    require(s.size() <= maximum && (!nonempty || !s.empty()) && s.find('\0') == std::string::npos,
            "Localization string exceeds its bounds or contains NUL");
    return s;
}
} // namespace
bool ui_locale_tag(std::string_view value) noexcept {
    if (value.size() < 2 || value.size() > 35)
        return false;
    std::size_t segment = 0;
    bool primary = true;
    for (unsigned char c : value) {
        if (c == '-') {
            if (segment < (primary ? 2u : 1u) || segment > 8)
                return false;
            primary = false;
            segment = 0;
        } else {
            if (!letter(c) && (primary || !digit(c)))
                return false;
            ++segment;
        }
    }
    return segment >= (primary ? 2u : 1u) && segment <= 8;
}
std::string canonical_ui_locale(std::string_view value) {
    require(ui_locale_tag(value),
            "Locale tag requires bounded ASCII language and hyphenated segments");
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value)
        result.push_back(c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : char(c));
    return result;
}
bool ui_localization_key(std::string_view value) {
    return !value.empty() && value.size() <= 48 &&
           (letter(static_cast<unsigned char>(value.front())) || value.front() == '_') &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return letter(c) || digit(c) || c == '_'; });
}
std::string UiLocalizationData::select_locale(std::string_view requested) const {
    const auto tag = canonical_ui_locale(requested);
    if (locales.contains(tag))
        return tag;
    if (const auto at = tag.find('-'); at != std::string::npos) {
        auto primary = tag.substr(0, at);
        if (locales.contains(primary))
            return primary;
    }
    return default_locale;
}
UiLocalizedMessage UiLocalizationData::message(std::string_view key,
                                               std::string_view requested) const {
    const auto locale = select_locale(requested);
    if (!ui_localization_key(key))
        return {"[[invalid key]]", locale, false, true};
    const std::string name(key);
    const auto& messages = locales.at(locale).messages;
    if (const auto at = messages.find(name); at != messages.end())
        return {at->second, locale, locale != canonical_ui_locale(requested), false};
    const auto& defaults = locales.at(default_locale).messages;
    if (const auto at = defaults.find(name); at != defaults.end())
        return {at->second, default_locale, true, false};
    return {"[[" + name + "]]", locale, false, true};
}
std::size_t UiLocalizationData::resident_bytes() const {
    auto size = sizeof(*this) + default_locale.capacity();
    for (const auto& [tag, locale] : locales) {
        size += sizeof(decltype(locales)::value_type) + tag.capacity() + locale.label.capacity();
        for (const auto& [key, message] : locale.messages)
            size +=
                sizeof(decltype(locale.messages)::value_type) + key.capacity() + message.capacity();
    }
    return size;
}
UiLocalizationSource UiLocalizationSource::create(AssetId id) {
    require(bool(id), "Localization requires an asset identity");
    return {{{"format", "forge.ui-localization"},
             {"version", 1},
             {"asset_id", id},
             {"default_locale", "en"},
             {"locales",
              {{"en", {{"label", "English"}, {"messages", {{"menu_play", "Play"}}}}},
               {"fr", {{"label", "Français"}, {"messages", {{"menu_play", "Jouer"}}}}}}}}};
}
UiLocalizationSource UiLocalizationSource::parse(std::span<const std::byte> bytes) {
    UiLocalizationSource source{
        asset_detail::parse_bounded_json(bytes, ui_localization_byte_limit, 12000, 8)};
    source.validate();
    return source;
}
AssetId UiLocalizationSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void UiLocalizationSource::validate() const {
    const auto encoded = document.dump(); // Native DOM edits also require valid UTF-8.
    require(asset_detail::parse_bounded_json(std::as_bytes(std::span(encoded)),
                                             ui_localization_byte_limit, 12000, 8) == document,
            "Localization must survive JSON save and reload without value changes");
    require(document.is_object() && document.at("format") == "forge.ui-localization" &&
                document.at("version").is_number_integer() && document.at("version") == 1 &&
                bool(asset()),
            "Unsupported localization source or missing identity");
    const auto default_tag = canonical_ui_locale(text(document.at("default_locale"), 35, true));
    const auto& rows = document.at("locales");
    require(rows.is_object() && !rows.empty() && rows.size() <= 8,
            "Localization requires 1–8 locales");
    const Json* default_messages = nullptr;
    std::set<std::string> tags;
    for (const auto& [name, row] : rows.items()) {
        const auto tag = canonical_ui_locale(name);
        require(tags.insert(tag).second, "Locale tags differ only by case");
        require(row.is_object(), "Locale data must be an object");
        (void)text(row.at("label"), 64, true);
        const auto& messages = row.at("messages");
        require(messages.is_object() && messages.size() <= 128,
                "A locale supports at most 128 messages");
        for (const auto& [key, value] : messages.items()) {
            require(ui_localization_key(key), "Message keys need 1–48 ASCII identifier bytes");
            (void)text(value, 1024, false);
        }
        if (tag == default_tag)
            default_messages = &messages;
    }
    require(default_messages && !default_messages->empty(),
            "Default locale must exist and contain messages");
    for (const auto& row : rows)
        for (const auto& [key, value] : row.at("messages").items()) {
            (void)value;
            require(default_messages->contains(key),
                    "Translated key is absent from default locale");
        }
}
UiLocalizationData UiLocalizationSource::compiled() const {
    validate();
    UiLocalizationData result{
        asset(),
        canonical_ui_locale(document.at("default_locale").get_ref<const std::string&>()),
        {}};
    for (const auto& [name, row] : document.at("locales").items()) {
        UiLocalizationLocale locale;
        locale.label = row.at("label");
        for (const auto& [key, value] : row.at("messages").items())
            locale.messages.emplace(key, value.get<std::string>());
        result.locales.emplace(canonical_ui_locale(name), std::move(locale));
    }
    return result;
}
std::vector<std::byte> UiLocalizationSource::cook() const {
    validate();
    const auto text = document.dump();
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
} // namespace forge
