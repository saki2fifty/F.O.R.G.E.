#include <algorithm>
#include <cmath>
#include <forge/import_settings.hpp>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
bool token(std::string_view value) {
    return !value.empty() && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-' || c == '.';
           });
}
void bounded(const Json& value) {
    (void)asset_build_digest(value);
    if (value.dump().size() > 64 * 1024)
        throw std::runtime_error("Import settings exceed 64 KiB");
}
void identity(const ImportSettingsDocument& value) {
    if (!token(value.importer) || !value.version || !value.overrides.is_object() ||
        !value.unknown.is_object())
        throw std::runtime_error("Invalid import settings identity/version/object");
    for (const auto* reserved : {"format", "version", "settings_version", "importer", "overrides"})
        if (value.unknown.contains(reserved))
            throw std::runtime_error("Unknown import settings cannot shadow reserved fields");
}
} // namespace
void to_json(nlohmann::json& output, const ImportSettingsDocument& settings) {
    identity(settings);
    auto result = settings.unknown;
    result["format"] = "forge.import-settings";
    result["version"] = 1;
    result["settings_version"] = settings.version;
    result["importer"] = settings.importer;
    result["overrides"] = settings.overrides;
    bounded(result);
    output = std::move(result);
}
void from_json(const nlohmann::json& input, ImportSettingsDocument& settings) {
    bounded(input);
    if (!input.is_object() || input.at("format") != "forge.import-settings" ||
        !input.at("version").is_number_integer() || input.at("version") != 1)
        throw std::runtime_error("Invalid import settings format");
    const auto& version = input.at("settings_version");
    if ((!version.is_number_integer() && !version.is_number_unsigned()) ||
        (version.is_number_integer() && !version.is_number_unsigned() &&
         version.get<std::int64_t>() < 1) ||
        version.get<std::uint64_t>() > UINT32_MAX || version.get<std::uint64_t>() == 0)
        throw std::runtime_error("Invalid import settings version");
    ImportSettingsDocument candidate;
    candidate.version = version.get<unsigned>();
    candidate.importer = input.at("importer").get<std::string>();
    candidate.overrides = input.at("overrides");
    candidate.unknown = input;
    for (const auto* field : {"format", "version", "settings_version", "importer", "overrides"})
        candidate.unknown.erase(field);
    identity(candidate);
    settings = std::move(candidate);
}
ImportSettingsSchema::ImportSettingsSchema(std::string importer, unsigned version,
                                           std::vector<ImportSettingRule> rules, Validator validate)
    : importer_(std::move(importer)), version_(version), rules_(std::move(rules)),
      validate_(std::move(validate)) {
    if (!token(importer_) || !version_ || rules_.size() > 256)
        throw std::runtime_error("Invalid importer settings schema identity/size");
    std::set<std::string> keys;
    std::size_t text_bytes = 0;
    for (const auto& rule : rules_) {
        if (!token(rule.key) || !keys.insert(rule.key).second || rule.label.empty() ||
            rule.help.empty() || rule.label.size() > 256 || rule.help.size() > 4096 ||
            rule.max_length > 65536 || rule.max_entries > 4096 || rule.choices.size() > 256)
            throw std::runtime_error("Invalid import setting definition: " + rule.key);
        if ((rule.minimum && !std::isfinite(*rule.minimum)) ||
            (rule.maximum && !std::isfinite(*rule.maximum)) ||
            (rule.minimum && rule.maximum && *rule.minimum > *rule.maximum))
            throw std::runtime_error("Invalid import setting numeric range: " + rule.key);
        if ((rule.minimum || rule.maximum) && rule.type != ImportSettingType::Integer &&
            rule.type != ImportSettingType::Number)
            throw std::runtime_error("Numeric range on nonnumeric import setting: " + rule.key);
        std::set<std::string> choices;
        for (const auto& choice : rule.choices) {
            if (choice.size() > rule.max_length || choice.find('\0') != std::string::npos ||
                !choices.insert(choice).second)
                throw std::runtime_error("Invalid import setting choices: " + rule.key);
            text_bytes += choice.size();
        }
        if ((rule.type == ImportSettingType::Choice && rule.choices.empty()) ||
            (rule.type != ImportSettingType::Choice && rule.type != ImportSettingType::StringList &&
             !rule.choices.empty()))
            throw std::runtime_error("Choice values do not match import setting kind: " + rule.key);
        text_bytes += rule.key.size() + rule.label.size() + rule.help.size();
        if (text_bytes > 1024 * 1024)
            throw std::runtime_error("Import settings schema text exceeds 1 MiB");
        validate_value(rule, rule.default_value);
    }
    const auto values = defaults();
    bounded(values);
    if (validate_)
        validate_(values);
}
void ImportSettingsSchema::validate_value(const ImportSettingRule& rule, const Json& value) const {
    auto fail = [&] { throw std::runtime_error("Invalid import setting value: " + rule.key); };
    auto text = [&](const Json& item) {
        if (!item.is_string())
            fail();
        const auto& str = item.get_ref<const std::string&>();
        if (str.size() > rule.max_length || str.find('\0') != std::string::npos)
            fail();
    };
    switch (rule.type) {
    case ImportSettingType::Boolean:
        if (!value.is_boolean())
            fail();
        break;
    case ImportSettingType::Integer:
        if (!value.is_number_integer() && !value.is_number_unsigned())
            fail();
        // Descriptor bounds are binary64; keep integral comparison exact across
        // platforms where long double has no more precision than double.
        if ((value.is_number_unsigned() && value.get<std::uint64_t>() > 9007199254740991ull) ||
            (!value.is_number_unsigned() && (value.get<std::int64_t>() < -9007199254740991ll ||
                                             value.get<std::int64_t>() > 9007199254740991ll)))
            fail();
        [[fallthrough]];
    case ImportSettingType::Number:
        if (!value.is_number() || !std::isfinite(value.get<double>()))
            fail();
        if ((rule.minimum && value.get<double>() < *rule.minimum) ||
            (rule.maximum && value.get<double>() > *rule.maximum))
            fail();
        break;
    case ImportSettingType::Text:
        text(value);
        break;
    case ImportSettingType::Choice:
        text(value);
        if (std::find(rule.choices.begin(), rule.choices.end(), value.get<std::string>()) ==
            rule.choices.end())
            fail();
        break;
    case ImportSettingType::StringList: {
        if (!value.is_array() || value.size() > rule.max_entries)
            fail();
        std::set<std::string> selected;
        for (const auto& item : value) {
            text(item);
            if (!rule.choices.empty()) {
                const auto& choice = item.get_ref<const std::string&>();
                if (std::find(rule.choices.begin(), rule.choices.end(), choice) ==
                        rule.choices.end() ||
                    !selected.insert(choice).second)
                    fail();
            }
        }
        break;
    }
    case ImportSettingType::StringMap:
        if (!value.is_object() || value.size() > rule.max_entries)
            fail();
        for (const auto& [key, item] : value.items()) {
            if (key.empty() || key.size() > rule.max_length || key.find('\0') != std::string::npos)
                fail();
            text(item);
        }
        break;
    default:
        fail();
    }
}
nlohmann::json ImportSettingsSchema::defaults() const {
    auto result = Json::object();
    for (const auto& rule : rules_)
        result[rule.key] = rule.default_value;
    return result;
}
nlohmann::json ImportSettingsSchema::effective(const ImportSettingsDocument& settings) const {
    const Json checked = settings;
    (void)checked;
    if (settings.importer != importer_ || settings.version != version_)
        throw std::runtime_error(
            "Import settings need the matching importer and explicit version migration");
    auto result = defaults();
    for (const auto& [key, value] : settings.overrides.items()) {
        const auto found = std::find_if(rules_.begin(), rules_.end(),
                                        [&](const auto& rule) { return rule.key == key; });
        if (found == rules_.end())
            throw std::runtime_error(
                "Unrecognized import setting is preserved but cannot be applied: " + key);
        validate_value(*found, value);
        result[key] = value;
    }
    bounded(result);
    if (validate_)
        validate_(result);
    return result;
}
ImportSettingsDocument ImportSettingsSchema::edit(const ImportSettingsDocument& settings,
                                                  std::string_view key,
                                                  std::optional<Json> value) const {
    const Json checked = settings;
    (void)checked;
    if (settings.importer != importer_ || settings.version != version_)
        throw std::runtime_error("Cannot edit settings for a different importer/version");
    if (std::none_of(rules_.begin(), rules_.end(),
                     [&](const auto& rule) { return rule.key == key; }))
        throw std::runtime_error("Cannot edit unknown import setting: " + std::string(key));
    auto result = settings;
    if (value)
        result.overrides[std::string(key)] = std::move(*value);
    else
        result.overrides.erase(std::string(key));
    (void)effective(result);
    return result;
}
ImportSettingsDocument ImportSettingsSchema::reset(const ImportSettingsDocument& settings) const {
    const Json checked = settings;
    (void)checked;
    if (settings.importer != importer_ || settings.version != version_)
        throw std::runtime_error("Cannot reset settings for a different importer/version");
    auto result = settings;
    result.overrides = Json::object();
    (void)effective(result);
    return result;
}
ImportSettingsDocument ImportSettingsSchema::migrate(const ImportSettingsDocument& settings,
                                                     const Migrator& migrate) const {
    const Json checked = settings;
    (void)checked;
    if (settings.importer != importer_ || settings.version > version_)
        throw std::runtime_error("Import settings migration cannot change importer or downgrade");
    if (settings.version == version_) {
        (void)effective(settings);
        return settings;
    }
    if (!migrate)
        throw std::runtime_error("Import settings version has no explicit migrator");
    auto result = migrate(settings);
    if (result.unknown != settings.unknown)
        throw std::runtime_error("Import settings migration must preserve unknown envelope data");
    (void)effective(result);
    return result;
}
std::string ImportSettingsSchema::digest(const ImportSettingsDocument& settings) const {
    return asset_build_digest(
        {{"importer", importer_}, {"version", version_}, {"values", effective(settings)}});
}
} // namespace forge
