#include <forge/ui_localization.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid localization source was accepted");
}
UiLocalizationSource parse(std::string_view text) {
    return UiLocalizationSource::parse(std::as_bytes(std::span(text)));
}
} // namespace
int main() {
    try {
        auto source = UiLocalizationSource::create(AssetId::generate());
        source.document["locales"]["en"]["messages"]["literal"] =
            "<b>Crème & café</b> {{not_a_binding}}";
        source.document["vendor"] = {{"note", "Opaque extension"}};
        const auto bytes = source.cook();
        const auto reopened = UiLocalizationSource::parse(bytes);
        check(reopened.document == source.document && reopened.asset() == source.asset(),
              "Save/cook changed localization identity or extension data");
        const auto data = reopened.compiled();
        const auto french = data.message("menu_play", "FR-ca");
        check(french.text == "Jouer" && french.locale == "fr" && french.fallback,
              "Case-insensitive primary-language fallback failed");
        check(data.message("menu_play", "fr").fallback == false,
              "An exact locale was marked as fallback");
        const auto untranslated = data.message("literal", "fr");
        check(nlohmann::json(untranslated.text) ==
                      source.document["locales"]["en"]["messages"]["literal"] &&
                  untranslated.locale == "en" && untranslated.fallback && !untranslated.missing,
              "Missing translation changed literal bytes or bypassed the default locale");
        check(data.select_locale("de-CH") == "en" && data.message("menu_play", "de").fallback,
              "Unsupported locale did not select the default");
        check(data.message("absent", "en").text == "[[absent]]" &&
                  data.message("absent", "en").missing &&
                  data.message("<img>", "en").text == "[[invalid key]]",
              "Missing/invalid key markers were not bounded literal values");
        source.document["locales"]["en"]["messages"]["menu_play"] = "Start";
        check(data.message("menu_play", "en").text == "Play" &&
                  source.compiled().message("menu_play", "en").text == "Start",
              "Editing source mutated a retained compiled table");
        for (const auto* tag : {"e", "1en", "en_Us", "en--us", "en-", "toolonglanguage", "éé"})
            rejects([&] { (void)canonical_ui_locale(tag); });
        check(canonical_ui_locale("EN-Latn-419") == "en-latn-419",
              "Supported ASCII tag segments were lost");
        const auto invalid = [&](const auto& edit) {
            auto candidate = source;
            edit(candidate.document);
            rejects([&] { candidate.validate(); });
        };
        invalid([](auto& d) { d["default_locale"] = "es"; });
        invalid([](auto& d) { d["locales"]["EN"] = d["locales"]["en"]; });
        invalid([](auto& d) { d["locales"]["fr"]["messages"]["unknown"] = "Autre"; });
        invalid(
            [](auto& d) { d["locales"]["en"]["messages"]["menu_play"] = std::string(1025, 'x'); });
        invalid([](auto& d) { d["locales"]["en"]["messages"]["bad.key"] = "Invalid"; });
        invalid([](auto& d) { d["locales"]["en"]["label"] = ""; });
        invalid([](auto& d) { d["locales"]["en"]["label"] = std::string(65, 'x'); });
        invalid([](auto& d) { d["asset_id"] = nullptr; });
        invalid([](auto& d) { d["version"] = 1.0; });
        invalid(
            [](auto& d) { d["locales"]["en"]["messages"]["menu_play"] = std::string("x\0y", 3); });
        invalid([](auto& d) { d["locales"]["en"]["label"] = std::string(1, char(0xff)); });
        invalid([](auto& d) { d["extension"] = std::numeric_limits<double>::quiet_NaN(); });
        invalid([](auto& d) {
            auto nested = nlohmann::json::object();
            for (unsigned i = 0; i < 10; ++i)
                nested = {{"child", nested}};
            d["extension"] = nested;
        });
        rejects([] { (void)parse("{\"format\":1,\"format\":2}"); });
        rejects([] { (void)parse(std::string("{}\0{}", 5)); });
        rejects([] { (void)parse(std::string(ui_localization_byte_limit + 1, ' ')); });
        auto bounded = UiLocalizationSource::create(source.asset());
        auto& master = bounded.document["locales"]["en"]["messages"];
        master = nlohmann::json::object();
        for (unsigned i = 0; i < 128; ++i)
            master["key_" + std::to_string(i)] = "Value";
        bounded.document["locales"]["fr"]["messages"] = nlohmann::json::object();
        for (const auto* tag : {"de", "es", "it", "pt", "nl", "sv"})
            bounded.document["locales"][tag] = {{"label", tag},
                                                {"messages", nlohmann::json::object()}};
        bounded.validate();
        check(bounded.compiled().message("key_127", "sv").fallback,
              "Largest admitted table did not retain missing-translation fallback");
        auto too_many = bounded;
        too_many.document["locales"]["en"]["messages"]["key_128"] = "Overflow";
        rejects([&] { too_many.validate(); });
        bounded.document["locales"]["da"] = {{"label", "Danish"},
                                             {"messages", nlohmann::json::object()}};
        rejects([&] { bounded.validate(); });
        check(ui_localization_key(std::string(48, 'a')) &&
                  !ui_localization_key(std::string(49, 'a')),
              "Message key bound is inconsistent");
        std::cout << "Localization codec, literal UTF-8, fallback, identity and bounded admission "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
