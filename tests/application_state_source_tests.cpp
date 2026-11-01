#include <forge/application_state_source.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid application state source accepted");
}
ApplicationStatesSource parse(const std::string& text) {
    return ApplicationStatesSource::parse(std::as_bytes(std::span(text)));
}
void roundtrip() {
    auto source = ApplicationStatesSource::create(AssetId::generate());
    source.document["extension"] = {{"future", Json::array({1, false, "opaque"})}};
    auto& app = source.document["domains"][0];
    app["states"].push_back({{"key", "live"}, {"label", "Live"}});
    app["states"][1]["parent"] = "live";
    app["states"][1]["extension"] = "Retain node metadata";
    source.document["domains"].push_back(
        {{"key", "round"},
         {"initial", "ready"},
         {"enabled_when", {{"domain", "app"}, {"state", "live"}, {"extension", true}}},
         {"states", Json::array({{{"key", "ready"}}, {{"key", "active"}}})}});
    source.document["domains"].push_back(
        {{"key", "hud"},
         {"computed", true},
         {"initial", "hidden"},
         {"states", Json::array({{{"key", "hidden"}}, {{"key", "visible"}}})},
         {"rules",
          Json::array({{{"state", "visible"},
                        {"all", Json::array({{{"domain", "app"}, {"state", "live"}},
                                             {{"domain", "round"}, {"state", "active"}}})}}})}});
    const auto bytes = source.cook();
    const auto restored = ApplicationStatesSource::parse(bytes);
    check(restored.document == source.document && restored.asset() == source.asset(),
          "Cook lost source identity or opaque extensions");
    const auto definition = restored.compiled();
    check(definition.domains.size() == 3 && definition.domains[1].label == "round" &&
              definition.domains[0].states[1].parent == "live" && definition.domains[2].computed &&
              definition.domains[2].rules[0].all.size() == 2,
          "Compiled state semantics changed");
    auto copy = definition;
    copy.domains[0].states[0].label = "Independent";
    check(definition.domains[0].states[0].label == "Menu", "Compiled values share mutable strings");
}
void invalid() {
    const auto original = ApplicationStatesSource::create(AssetId::generate());
    auto invalid_edit = [&](const auto& edit) {
        auto source = original;
        edit(source.document);
        rejects([&] { source.validate(); });
        rejects([&] { source.cook(); });
        rejects([&] { source.compiled(); });
    };
    invalid_edit([](Json& d) { d["version"] = 1.0; });
    invalid_edit([](Json& d) { d["asset_id"] = nullptr; });
    invalid_edit([](Json& d) { d["domains"][0]["computed"] = 1; });
    invalid_edit([](Json& d) { d["domains"][0]["initial"] = "missing"; });
    invalid_edit([](Json& d) { d["domains"][0]["states"][0]["parent"] = "menu"; });
    invalid_edit([](Json& d) { d["domains"][0]["states"][0]["key"] = "../../escape"; });
    invalid_edit([](Json& d) { d["domains"][0]["states"][0]["label"] = std::string("x\0y", 3); });
    invalid_edit([](Json& d) { d["domains"][0]["label"] = std::string(1, '\xff'); });
    invalid_edit([](Json& d) { d["extension"] = std::numeric_limits<double>::quiet_NaN(); });
    invalid_edit([](Json& d) { d["extension"] = std::string(application_states_byte_limit, 'a'); });
    invalid_edit([](Json& d) { d["domains"][0]["states"] = Json::array(); });
    invalid_edit([](Json& d) {
        d["domains"][0]["enabled_when"] = {{"domain", "missing"}, {"state", "menu"}};
    });
    invalid_edit([](Json& d) {
        d["domains"][0]["rules"] = Json::array({{{"state", "menu"}, {"all", Json::array()}}});
    });
    const auto encoded = original.document.dump();
    rejects([&] { parse("{\"format\":\"x\"," + encoded.substr(1)); });
    rejects([&] { parse(encoded + std::string("\0trailing", 9)); });
    rejects([&] { parse(std::string(application_states_byte_limit + 1, ' ')); });
    auto nested = original.document;
    for (int i = 0; i < 12; ++i)
        nested = Json::array({nested});
    rejects([&] { parse(nested.dump()); });
}
void limits() {
    const auto original = ApplicationStatesSource::create(AssetId::generate());
    auto source = original;
    auto& states = source.document["domains"][0]["states"];
    states = Json::array();
    for (unsigned i = 0; i < 64; ++i)
        states.push_back({{"key", "s" + std::to_string(i)}});
    source.document["domains"][0]["initial"] = "s0";
    source.validate();
    states.push_back({{"key", "over"}});
    rejects([&] { source.validate(); });
    source = original;
    auto& chain = source.document["domains"][0]["states"];
    chain = Json::array();
    for (unsigned i = 0; i < 16; ++i)
        chain.push_back(
            {{"key", "s" + std::to_string(i)}, {"parent", i ? "s" + std::to_string(i - 1) : ""}});
    source.document["domains"][0]["initial"] = "s15";
    source.validate();
    chain.push_back({{"key", "over"}, {"parent", "s15"}});
    rejects([&] { source.validate(); });
}
} // namespace
int main() {
    try {
        roundtrip();
        invalid();
        limits();
        std::cout << "Application state source: bounded graphs, typed validation, opaque "
                     "extensions and cook roundtrip passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
