#pragma once
#include "actions.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace forge::ui {
// Personal overrides for the existing action catalog, not another command registry.
class EditorShortcuts {
    using Json = nlohmann::json;

  public:
    using Keys = std::array<ImGuiKeyChord, 2>;
    bool open = false;
    const std::string& status() const { return status_; }
    bool capturing() const { return !recording_.empty() || wait_release_; }
    static std::string key_token(ImGuiKey key) {
        if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
            return std::string(1, char('A' + key - ImGuiKey_A));
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
            return std::string(1, char('0' + key - ImGuiKey_0));
        if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24)
            return "F" + std::to_string(1 + key - ImGuiKey_F1);
        for (const auto& [native, token] : special_keys())
            if (key == native)
                return token;
        return {};
    }
    static ImGuiKey key_from_token(const std::string& token) {
        // Forge owns this vocabulary. ImGui GetKeyName is debug-only and cannot
        // be used as a persisted or compared compatibility identifier.
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key)
            if (!token.empty() && key_token(ImGuiKey(key)) == token)
                return ImGuiKey(key);
        return ImGuiKey_None;
    }
    static std::string label(ImGuiKeyChord chord) {
        if (!chord)
            return "Unassigned";
        std::string text;
        for (const auto& [mod, name] : modifiers())
            if (chord & mod)
                text += std::string(name) + "+";
        return text + key_token(ImGuiKey(chord & ~ImGuiMod_Mask_));
    }
    static bool pressed(ImGuiKeyChord chord) {
        return chord && ImGui::GetIO().KeyMods == (chord & ImGuiMod_Mask_) &&
               ImGui::IsKeyPressed(ImGuiKey(chord & ~ImGuiMod_Mask_), false);
    }
    static std::string invalid(ImGuiKeyChord chord, const std::string& id = {}) {
        if (!chord)
            return {};
        const auto key = ImGuiKey(chord & ~ImGuiMod_Mask_);
        const auto mods = chord & ImGuiMod_Mask_;
        if (key_token(key).empty())
            return "Choose a supported keyboard key.";
        if (key == ImGuiKey_Backspace ||
            ((mods & ImGuiMod_Ctrl) &&
             (key == ImGuiKey_A || key == ImGuiKey_C || key == ImGuiKey_V || key == ImGuiKey_X ||
              key == ImGuiKey_F || (id == "save" && (key == ImGuiKey_Z || key == ImGuiKey_Y)))) ||
            (id == "save" && key >= ImGuiKey_Tab && key <= ImGuiKey_Escape))
            return "That combination is reserved for text editing or navigation.";
        if (key == ImGuiKey_Escape || key == ImGuiKey_Delete || key == ImGuiKey_F2 ||
            key == ImGuiKey_F6 || key == ImGuiKey_F7 ||
            ((mods & ImGuiMod_Alt) &&
             (key == ImGuiKey_F4 || key == ImGuiKey_Tab || key == ImGuiKey_Space)))
            return "That key is reserved for a contextual, window or gameplay control.";
        if ((mods & ImGuiMod_Ctrl) &&
            (key == ImGuiKey_Space || key == ImGuiKey_Tab || key == ImGuiKey_Minus ||
             key == ImGuiKey_Equal || key == ImGuiKey_0 ||
             ((mods & ImGuiMod_Shift) && key == ImGuiKey_P)))
            return "That combination is reserved for editor navigation or interface size.";
        if (!(mods & (ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super)) &&
            !(key >= ImGuiKey_F8 && key <= ImGuiKey_F24))
            return "Use Ctrl, Alt or Super with a key, or F8 through F24.";
        return {};
    }
    void load(const Json& value) {
        data_ = value;
        status_.clear();
        compatible_ = value.is_object() && value.contains("version") &&
                      value["version"].is_number_integer() && value["version"] == 1 &&
                      value.contains("bindings") && value["bindings"].is_object() &&
                      value["bindings"].size() <= 128;
        if (!compatible_)
            status_ = "Shortcut settings are unsupported. Defaults apply; reset to edit them.";
        rebuild();
    }
    Json settings() const { return data_; }
    Keys effective(const EditorAction& action) const {
        if (const auto at = parsed_.find(action.id); at != parsed_.end())
            return at->second;
        return action.default_keys;
    }
    Keys unique_keys(const EditorActions& actions, const std::string& id) const {
        const auto* action = actions.find(id);
        if (!action || !action->global_shortcut)
            return {};
        auto keys = effective(*action);
        for (const auto& other : actions.entries)
            if (other.global_shortcut && other.id != id)
                for (auto& chosen : keys)
                    for (auto existing : effective(other))
                        if (chosen && chosen == existing)
                            chosen = 0;
        return keys;
    }
    void apply(EditorActions& actions) const {
        for (auto& action : actions.entries)
            if (action.global_shortcut) {
                action.shortcut.clear();
                for (auto chord : effective(action))
                    if (chord) {
                        if (!action.shortcut.empty())
                            action.shortcut += " / ";
                        action.shortcut += label(chord);
                    }
            }
    }
    bool assign(const EditorActions& actions, const std::string& id, Keys keys,
                const std::function<void()>& save, bool reset = false) {
        try {
            if (!compatible_)
                throw std::runtime_error("Reset unsupported shortcut settings before editing.");
            const auto* action = actions.find(id);
            if (!action || !action->global_shortcut || id.size() > 256)
                throw std::runtime_error("This command does not offer a global shortcut.");
            if (reset)
                keys = action->default_keys;
            for (auto key : keys)
                if (const auto why = invalid(key, id); !why.empty())
                    throw std::runtime_error(why);
            if (keys[0] && keys[0] == keys[1])
                throw std::runtime_error("The two shortcuts must be different.");
            for (const auto& other : actions.entries)
                if (other.global_shortcut && other.id != id)
                    for (auto chosen : keys)
                        for (auto existing : effective(other))
                            if (chosen && chosen == existing)
                                throw std::runtime_error(label(chosen) + " is assigned to " +
                                                         other.label + ". Clear it there first.");
            auto next = data_;
            if (reset)
                next["bindings"].erase(id);
            else {
                if (!next["bindings"].contains(id) && next["bindings"].size() >= 128)
                    throw std::runtime_error("Personal shortcut limit (128 commands) reached.");
                next["bindings"][id] = Json::array({encode(keys[0]), encode(keys[1])});
            }
            return publish(std::move(next), save);
        } catch (const std::exception& e) {
            status_ = e.what();
            return false;
        }
    }
    bool reset_all(const std::function<void()>& save) {
        return publish({{"version", 1}, {"bindings", Json::object()}}, save);
    }
    // The caller supplies the existing mode/gesture/game-input gate. Widget and
    // popup ownership stays here so every global action uses the same rule.
    std::string dispatch(const EditorActions& actions, bool allowed) {
        release();
        if (!allowed || capturing() || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() ||
            ImGui::IsAnyMouseDown() ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            return {};
        const EditorAction* found = nullptr;
        for (const auto& action : actions.entries)
            if (action.global_shortcut)
                for (auto chord : effective(action))
                    if (pressed(chord)) {
                        if (found) {
                            status_ = "Conflicting shortcut settings; reset or clear the bindings.";
                            return {}; // Never guess priority for a hand-edited conflict.
                        }
                        found = &action;
                    }
        return found && actions.invoke(found->id) ? found->id : std::string();
    }
    void draw(const EditorActions& actions, const std::function<void()>& save, bool editable) {
        release();
        if (!open) {
            cancel_recording();
            return;
        }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize({std::min(900.f * interface_scale, viewport->WorkSize.x * .95f),
                                  std::min(620.f * interface_scale, viewport->WorkSize.y * .95f)},
                                 ImGuiCond_Appearing);
        const bool expanded = ImGui::Begin("Editor shortcuts", &open);
        if (expanded) {
            ImGui::TextWrapped("Personal editor commands. Typing, viewport tools and gameplay "
                               "input keep their own controls.");
            if (!editable)
                ImGui::TextWrapped("Stop Play and finish the active operation to edit shortcuts.");
            ImGui::InputText("Search commands", search_, sizeof(search_));
            FORGE_UI_PROBE("shortcuts:search");
            ImGui::BeginDisabled(!editable);
            if (ui::button("Reset all shortcuts", "Restore default global bindings."))
                reset_all(save);
            FORGE_UI_PROBE("shortcuts:reset_all");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Close shortcuts", "Return to the editor; changes are already saved."))
                open = false;
            FORGE_UI_PROBE("shortcuts:close");
            if (!status_.empty())
                ImGui::TextWrapped("%s", status_.c_str());
            const float height =
                std::max(80.f * interface_scale, ImGui::GetContentRegionAvail().y -
                                                     ImGui::GetTextLineHeightWithSpacing() * 3);
            if (ImGui::BeginTable("##editor shortcuts", 3,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_ScrollY,
                                  {0, height})) {
                ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 3);
                ImGui::TableSetupColumn("Shortcuts", ImGuiTableColumnFlags_WidthStretch, 3);
                ImGui::TableSetupColumn("Defaults", ImGuiTableColumnFlags_WidthFixed,
                                        80 * interface_scale);
                ImGui::TableHeadersRow();
                for (const auto& action : actions.entries) {
                    if (!action.global_shortcut || !command_matches(action.label, search_))
                        continue;
                    ImGui::PushID(action.id.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", action.label.c_str());
                    ui::help(action.description.c_str());
                    const bool personal = compatible_ && data_["bindings"].contains(action.id);
                    if (personal)
                        ImGui::TextDisabled(
                            "Personal override%s",
                            parsed_.contains(action.id) ? "" : " (invalid; using defaults)");
                    ImGui::TableNextColumn();
                    const auto keys = effective(action);
                    ImGui::BeginDisabled(!editable || !compatible_);
                    for (unsigned slot = 0; slot < keys.size(); ++slot) {
                        ImGui::PushID(int(slot));
                        ImGui::TextWrapped("%s", label(keys[slot]).c_str());
                        if (ui::button("Change...", "Record a shortcut for this command.")) {
                            recording_ = action.id;
                            recording_slot_ = slot;
                            request_popup_ = true;
                            status_.clear();
                        }
                        FORGE_UI_PROBE("shortcuts:record:" + action.id + ":" +
                                       std::to_string(slot));
                        ImGui::SameLine();
                        if (ui::button("Clear",
                                       "Remove this shortcut; other bindings stay stored.")) {
                            auto next = keys;
                            next[slot] = 0;
                            assign(actions, action.id, next, save);
                        }
                        FORGE_UI_PROBE("shortcuts:clear:" + action.id + ":" + std::to_string(slot));
                        ImGui::PopID();
                    }
                    ImGui::TableNextColumn();
                    if (ui::button("Reset", "Restore this command's default shortcuts."))
                        assign(actions, action.id, {}, save, true);
                    FORGE_UI_PROBE("shortcuts:reset:" + action.id);
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TextWrapped("Fixed controls: Esc/F6/F7 gameplay safety; Ctrl+Shift+P palette; "
                               "Ctrl+Space workspace; interface zoom, Hierarchy F2/Delete, "
                               "viewport tools and ordinary text editing.");
            if (request_popup_) {
                ImGui::OpenPopup("Record editor shortcut");
                request_popup_ = false;
            }
            ImGui::SetNextWindowSize(
                {std::min(600.f * interface_scale, viewport->WorkSize.x * .9f), 0},
                ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Record editor shortcut", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto* action = actions.find(recording_);
                ImGui::TextWrapped("%s", action ? action->label.c_str() : "Command unavailable");
                ImGui::TextWrapped("Press Ctrl, Alt or Super with a key, or F8 through F24.");
                ImGui::TextWrapped("Escape cancels. Reserved keys remain with their owners.");
                if (!status_.empty())
                    ImGui::TextWrapped("%s", status_.c_str());
                bool finished = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                if (!finished && action && editable)
                    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key) {
                        if ((key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper) ||
                            !ImGui::IsKeyPressed(ImGuiKey(key), false))
                            continue;
                        auto next = effective(*action);
                        next[recording_slot_] = key | ImGui::GetIO().KeyMods;
                        finished = assign(actions, action->id, next, save);
                        break;
                    }
                if (ui::button("Cancel", "Keep the existing shortcut."))
                    finished = true;
                FORGE_UI_PROBE("shortcuts:cancel_recording");
                if (finished) {
                    cancel_recording();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();
        if (!open)
            cancel_recording();
    }

  private:
    Json data_{{"version", 1}, {"bindings", Json::object()}};
    bool compatible_ = true, wait_release_ = false, request_popup_ = false;
    unsigned recording_slot_ = 0;
    char search_[256]{};
    std::string status_, recording_;
    std::map<std::string, Keys> parsed_; // Disposable decode cache; never another preference owner.
    void cancel_recording() {
        if (!recording_.empty())
            wait_release_ = true;
        recording_.clear();
        request_popup_ = false;
    }
    static const std::array<std::pair<int, const char*>, 4>& modifiers() {
        static constexpr std::array<std::pair<int, const char*>, 4> values{
            {{ImGuiMod_Ctrl, "Ctrl"},
             {ImGuiMod_Shift, "Shift"},
             {ImGuiMod_Alt, "Alt"},
             {ImGuiMod_Super, "Super"}}};
        return values;
    }
    static const std::array<std::pair<ImGuiKey, const char*>, 24>& special_keys() {
        static constexpr std::array<std::pair<ImGuiKey, const char*>, 24> values{
            {{ImGuiKey_Tab, "Tab"},
             {ImGuiKey_LeftArrow, "Left"},
             {ImGuiKey_RightArrow, "Right"},
             {ImGuiKey_UpArrow, "Up"},
             {ImGuiKey_DownArrow, "Down"},
             {ImGuiKey_PageUp, "PageUp"},
             {ImGuiKey_PageDown, "PageDown"},
             {ImGuiKey_Home, "Home"},
             {ImGuiKey_End, "End"},
             {ImGuiKey_Insert, "Insert"},
             {ImGuiKey_Delete, "Delete"},
             {ImGuiKey_Backspace, "Backspace"},
             {ImGuiKey_Space, "Space"},
             {ImGuiKey_Enter, "Enter"},
             {ImGuiKey_Escape, "Escape"},
             {ImGuiKey_Apostrophe, "Apostrophe"},
             {ImGuiKey_Comma, "Comma"},
             {ImGuiKey_Minus, "Minus"},
             {ImGuiKey_Period, "Period"},
             {ImGuiKey_Slash, "Slash"},
             {ImGuiKey_Semicolon, "Semicolon"},
             {ImGuiKey_Equal, "Equal"},
             {ImGuiKey_LeftBracket, "LeftBracket"},
             {ImGuiKey_RightBracket, "RightBracket"}}};
        return values;
    }
    static Json encode(ImGuiKeyChord chord) {
        if (!chord)
            return nullptr;
        Json value{{"key", key_token(ImGuiKey(chord & ~ImGuiMod_Mask_))}};
        for (const auto& [mod, name] : modifiers())
            value[name] = bool(chord & mod);
        return value;
    }
    static std::optional<Keys> decode(const Json& value, const std::string& id) {
        if (!value.is_array() || value.size() > 2)
            return std::nullopt;
        Keys result{};
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto& item = value[i];
            if (item.is_null())
                continue;
            if (!item.is_object() || !item.contains("key") || !item["key"].is_string())
                return std::nullopt;
            const auto token = item["key"].get<std::string>();
            if (token.size() > 32)
                return std::nullopt;
            result[i] = key_from_token(token);
            if (!result[i])
                return std::nullopt;
            for (const auto& [mod, name] : modifiers())
                if (const auto at = item.find(name); at != item.end()) {
                    if (!at->is_boolean())
                        return std::nullopt;
                    if (at->get<bool>())
                        result[i] |= mod;
                }
            if (!invalid(result[i], id).empty())
                return std::nullopt;
        }
        if (result[0] && result[0] == result[1])
            return std::nullopt;
        return result;
    }
    bool publish(Json next, const std::function<void()>& save) {
        const auto previous = data_;
        const auto was_compatible = compatible_;
        data_ = std::move(next);
        compatible_ = true;
        try {
            save(); // Existing application preference writer; synchronous owner thread.
            rebuild();
            status_ = "Shortcuts saved.";
            return true;
        } catch (const std::exception& e) {
            data_ = previous;
            compatible_ = was_compatible;
            status_ = e.what();
            return false;
        }
    }
    void rebuild() {
        parsed_.clear();
        if (compatible_)
            for (const auto& [id, value] : data_.at("bindings").items())
                if (id.size() <= 256)
                    if (const auto keys = decode(value, id))
                        parsed_.emplace(id, *keys);
    }
    void release() {
        if (!wait_release_)
            return;
        const auto& io = ImGui::GetIO();
        if (io.KeyMods)
            return;
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key)
            if (ImGui::IsKeyDown(ImGuiKey(key)))
                return;
        wait_release_ = false;
    }
};
} // namespace forge::ui
