#include <cmath>
#include <forge/game_settings.hpp>
#include <set>
namespace forge {
using Json = nlohmann::json;
namespace {
void number(const Json& value, double lo, double hi, const char* label, bool integer = false) {
    if (!value.is_number() || (integer && !value.is_number_integer()))
        throw std::runtime_error(std::string("game.settings: Invalid ") + label);
    const double n = value.get<double>();
    if (!std::isfinite(n) || n < lo || n > hi)
        throw std::runtime_error(std::string("game.settings: Out of range ") + label);
}
void object(const Json& value, const char* label) {
    if (!value.is_object())
        throw std::runtime_error(std::string("game.settings: Expected object for ") + label);
}
void display(const Json& value) {
    object(value, "display");
    const auto mode = value.at("mode").get<std::string>();
    if (mode != "windowed" && mode != "borderless" && mode != "fullscreen")
        throw std::runtime_error("game.settings: Unknown display mode");
    number(value.at("width"), 1, 32768, "width", true);
    number(value.at("height"), 1, 32768, "height", true);
    if (!value.at("vsync").is_boolean())
        throw std::runtime_error("game.settings: VSync must be boolean");
    // Display selection is an ordinal preference, resolved against current devices
    // by the platform host. It is not a persisted SDL display handle.
    number(value.at("display"), 0, 255, "display ordinal", true);
}
} // namespace
bool valid_application_id(std::string_view id) {
    if (id.empty() || id.size() > 128 || id.front() == '.' || id.back() == '.' ||
        id.find("..") != std::string_view::npos || id.find('.') == std::string_view::npos)
        return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return false;
    return true;
}
Json default_game_settings(std::string application_id, std::string title) {
    Json result = {{"version", 1},
                   {"application_id", std::move(application_id)},
                   {"title", std::move(title)},
                   {"profile", "development"},
                   {"save_schema", 1},
                   {"display",
                    {{"mode", "windowed"},
                     {"width", 1280},
                     {"height", 720},
                     {"display", 0},
                     {"vsync", false}}},
                   {"audio", {{"master_volume", 1.0}}},
                   {"input", {{"mouse_sensitivity", 1.0}}}};
    validate_game_settings(result);
    return result;
}
void validate_game_settings(const Json& data) {
    object(data, "game");
    number(data.at("version"), 1, 1, "version", true);
    if (!valid_application_id(data.at("application_id").get<std::string>()))
        throw std::runtime_error("game.settings: Expected a lowercase application ID such as "
                                 "com.example.game");
    const auto title = data.at("title").get<std::string>();
    if (title.empty() || title.size() > 256 || title.find('\0') != std::string::npos)
        throw std::runtime_error("game.settings: Invalid game title");
    const auto profile = data.at("profile").get<std::string>();
    if (profile != "development" && profile != "shipping")
        throw std::runtime_error("game.settings: Unknown runtime profile");
    number(data.at("save_schema"), 1, 1000000, "save schema", true);
    display(data.at("display"));
    object(data.at("audio"), "audio");
    number(data.at("audio").at("master_volume"), 0, 1, "master volume");
    object(data.at("input"), "input");
    number(data.at("input").at("mouse_sensitivity"), .01, 100, "mouse sensitivity");
}
Json resolve_game_settings(const Json& defaults, const Json& user, const InputMap& project_input) {
    validate_game_settings(defaults);
    object(user, "user settings");
    if (user.dump().size() > 1024 * 1024)
        throw std::runtime_error("game.settings: User settings exceed 1 MiB");
    Json result = defaults;
    for (const auto& [key, value] : user.items()) {
        if (key != "display" && key != "audio" && key != "input")
            throw std::runtime_error("game.settings: Unsupported user setting: " + key);
        object(value, key.c_str());
        for (const auto& [member, item] : value.items()) {
            if (key == "input" && member == "bindings")
                continue;
            const bool allowed =
                (key == "display" && (member == "mode" || member == "width" || member == "height" ||
                                      member == "display" || member == "vsync")) ||
                (key == "audio" && member == "master_volume") ||
                (key == "input" && member == "mouse_sensitivity");
            if (!allowed)
                throw std::runtime_error("game.settings: Unsupported user setting: " + key + "." +
                                         member);
            result[key][member] = item;
        }
    }
    validate_game_settings(result);
    Json map = project_input.source();
    if (user.contains("input") && user.at("input").contains("bindings")) {
        const auto& bindings = user.at("input").at("bindings");
        object(bindings, "binding overrides");
        for (const auto& [text, value] : bindings.items()) {
            const auto id = ActionId::parse(text);
            bool found = false;
            for (auto& action : map["actions"])
                if (action.at("id").get<ActionId>() == id) {
                    action["bindings"] = value;
                    found = true;
                    break;
                }
            if (!found)
                throw std::runtime_error("game.settings: Binding targets a missing ActionId: " +
                                         text);
        }
    }
    result["input_map"] = InputMap(std::move(map)).source();
    return result;
}
} // namespace forge
