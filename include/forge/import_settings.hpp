#pragma once
#include <forge/asset_build.hpp>
#include <optional>

namespace forge {
// Importer configuration only. Gameplay component metadata remains Flecs Meta.
enum class ImportSettingType { Boolean, Integer, Number, Text, Choice, StringList, StringMap };
struct ImportSettingRule {
    std::string key, label, help;
    ImportSettingType type = ImportSettingType::Text;
    nlohmann::json default_value;
    std::optional<double> minimum, maximum;
    std::vector<std::string> choices;
    std::size_t max_length = 4096, max_entries = 256;
};
struct ImportSettingsDocument {
    std::string importer;
    unsigned version = 1;
    nlohmann::json overrides = nlohmann::json::object();
    nlohmann::json unknown = nlohmann::json::object();
};
void to_json(nlohmann::json& value, const ImportSettingsDocument& settings);
void from_json(const nlohmann::json& value, ImportSettingsDocument& settings);
class ImportSettingsSchema {
  public:
    using Validator = std::function<void(const nlohmann::json&)>;
    using Migrator = std::function<ImportSettingsDocument(const ImportSettingsDocument&)>;
    ImportSettingsSchema(std::string importer, unsigned version,
                         std::vector<ImportSettingRule> rules, Validator validate = {});
    const std::string& importer() const { return importer_; }
    unsigned version() const { return version_; }
    const std::vector<ImportSettingRule>& rules() const { return rules_; }
    nlohmann::json defaults() const;
    nlohmann::json effective(const ImportSettingsDocument& settings) const;
    // Caller persists the returned candidate only after its owner transaction succeeds.
    // Equal-value overrides remain explicit. Reset removes the override entirely.
    ImportSettingsDocument edit(const ImportSettingsDocument& settings, std::string_view key,
                                std::optional<nlohmann::json> value) const;
    ImportSettingsDocument reset(const ImportSettingsDocument& settings) const;
    ImportSettingsDocument migrate(const ImportSettingsDocument& settings,
                                   const Migrator& migrate) const;
    std::string digest(const ImportSettingsDocument& settings) const;

  private:
    std::string importer_;
    unsigned version_;
    std::vector<ImportSettingRule> rules_;
    Validator validate_;
    void validate_value(const ImportSettingRule& rule, const nlohmann::json& value) const;
};
} // namespace forge
