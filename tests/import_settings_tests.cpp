#include <forge/import_settings.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid import settings accepted");
}
ImportSettingRule rule(std::string key, ImportSettingType type, Json value) {
    return {key, key, "Configure " + key, type, std::move(value)};
}
std::vector<ImportSettingRule> rules() {
    auto scale = rule("scale", ImportSettingType::Number, 1.0);
    scale.minimum = 0.001;
    scale.maximum = 1000;
    auto quality = rule("quality", ImportSettingType::Integer, 50);
    quality.minimum = 0;
    quality.maximum = 100;
    auto policy = rule("influences", ImportSettingType::Choice, "reject");
    policy.choices = {"reject", "reduce-four"};
    return {scale,
            quality,
            policy,
            rule("enabled", ImportSettingType::Boolean, true),
            rule("entry", ImportSettingType::Text, "main"),
            rule("roots", ImportSettingType::StringList, Json::array()),
            rule("defines", ImportSettingType::StringMap, Json::object())};
}
} // namespace
int main() {
    try {
        ImportSettingsSchema schema("forge.fixture", 1, rules(), [](const Json& values) {
            if (values.at("enabled") == false && values.at("quality") != 0)
                throw std::runtime_error("Disabled fixture requires quality zero");
        });
        ImportSettingsDocument initial{"forge.fixture"};
        initial.unknown["plugin-note"] = {{"future", {1, 2, 3}}};
        const auto defaults = schema.effective(initial);
        require(defaults.at("scale") == 1 && defaults.at("quality") == 50, "Defaults missing");
        const auto original = Json(initial);
        const auto base_key = schema.digest(initial);
        auto same = schema.edit(initial, "quality", 50);
        require(same.overrides.contains("quality") && schema.digest(same) == base_key,
                "Equal default override lost intent or changed effective identity");
        auto changed = schema.edit(same, "quality", 90);
        require(schema.digest(changed) != base_key && changed.unknown == initial.unknown,
                "Settings change did not affect build identity or lost opaque data");
        auto reset_one = schema.edit(changed, "quality", std::nullopt);
        require(!reset_one.overrides.contains("quality") && schema.digest(reset_one) == base_key,
                "Field reset did not remove override");
        auto reset_all = schema.reset(changed);
        require(reset_all.overrides.empty() && reset_all.unknown == initial.unknown,
                "Reset all failed or discarded opaque envelope");
        rejects([&] { schema.edit(initial, "quality", 1.5); });
        rejects([&] { schema.edit(initial, "quality", 101); });
        rejects([&] { schema.edit(initial, "quality", 9007199254740992ull); });
        rejects([&] { schema.edit(initial, "enabled", 1); });
        rejects([&] { schema.edit(initial, "scale", std::numeric_limits<double>::infinity()); });
        rejects([&] { schema.edit(initial, "influences", "discard"); });
        rejects([&] { schema.edit(initial, "unknown", 1); });
        rejects([&] { schema.edit(initial, "entry", std::string("a\0b", 3)); });
        rejects([&] { schema.edit(initial, "entry", std::string(4097, 'a')); });
        rejects([&] { schema.edit(initial, "roots", Json::array({1})); });
        rejects([&] { schema.edit(initial, "defines", Json{{"TEST", false}}); });
        rejects([&] { schema.edit(initial, "enabled", false); });
        require(Json(initial) == original, "Failed edit mutated the source document");
        auto copy = schema.edit(initial, "roots", Json::array({"Shaders", "Shared/Includes"}));
        copy = schema.edit(copy, "defines", Json{{"FEATURE", "1"}, {"NAME", "literal"}});
        require(Json(copy).get<ImportSettingsDocument>().overrides == copy.overrides,
                "String list/map override roundtrip failed");
        auto bad_saved = initial;
        bad_saved.overrides["quality"] = -1;
        require(schema.effective(schema.edit(bad_saved, "quality", 20)).at("quality") == 20,
                "Invalid known setting could not be repaired explicitly");
        bad_saved = initial;
        bad_saved.overrides["future-setting"] = {1, 2};
        const Json preserved = bad_saved;
        rejects([&] { schema.effective(bad_saved); });
        require(Json(bad_saved) == preserved, "Unknown override was silently removed");
        ImportSettingsSchema next("forge.fixture", 2, rules());
        rejects([&] { next.effective(initial); });
        rejects([&] { next.migrate(initial, {}); });
        auto migrated = next.migrate(changed, [](const auto& old) {
            auto candidate = old;
            candidate.version = 2;
            return candidate;
        });
        require(migrated.version == 2 && migrated.overrides.at("quality") == 90 &&
                    migrated.unknown == initial.unknown,
                "Explicit migration lost values/opaque data");
        rejects([&] {
            next.migrate(initial, [](const auto& old) {
                auto value = old;
                value.version = 2;
                value.unknown = Json::object();
                return value;
            });
        });
        rejects([&] { schema.migrate(migrated, [](const auto& old) { return old; }); });
        auto incompatible = original;
        incompatible["version"] = 1.5;
        auto output = initial;
        rejects([&] { from_json(incompatible, output); });
        require(Json(output) == original, "Failed parse modified prior document");
        incompatible = original;
        incompatible["version"] = 4294967296ull;
        rejects([&] { incompatible.get<ImportSettingsDocument>(); });
        incompatible = original;
        incompatible["huge"] = std::string(65536, 'x');
        rejects([&] { incompatible.get<ImportSettingsDocument>(); });
        auto shadow = initial;
        shadow.unknown["version"] = 99;
        rejects([&] { Json value = shadow; });
        auto definitions = rules();
        definitions.push_back(definitions[0]);
        rejects([&] { ImportSettingsSchema bad("forge.fixture", 1, definitions); });
        definitions = rules();
        definitions[0].minimum = 2000;
        rejects([&] { ImportSettingsSchema bad("forge.fixture", 1, definitions); });
        definitions = rules();
        definitions[2].choices = {"reject", "reject"};
        rejects([&] { ImportSettingsSchema bad("forge.fixture", 1, definitions); });
        definitions = rules();
        definitions[0].help.clear();
        rejects([&] { ImportSettingsSchema bad("forge.fixture", 1, definitions); });
        ImportSettingRule usages{"usages", "Usages", "Choose distinct supported usages",
                                 ImportSettingType::StringList, Json::array()};
        usages.choices = {"color", "data", "normal"};
        ImportSettingsSchema multi("forge.multi", 1, {usages});
        ImportSettingsDocument multi_value{"forge.multi"};
        multi_value = multi.edit(multi_value, "usages", Json::array({"color", "data"}));
        require(multi.effective(multi_value).at("usages").size() == 2,
                "Typed multi-choice values were lost");
        rejects([&] { multi.edit(multi_value, "usages", Json::array({"unknown"})); });
        rejects([&] { multi.edit(multi_value, "usages", Json::array({"color", "color"})); });
        std::cout << "Typed importer settings, explicit intent, migration, limits and build "
                     "identity passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
