#pragma once
#include "../texture_authoring.hpp"
#include "../ui_localization_authoring.hpp"
#include "editor_state.hpp"
#include "source_document_panel.hpp"
#include <array>
#include <forge/authoring.hpp>

namespace forge {
class UiLocalizationEditor : public SourceDocumentPanel<UiLocalizationDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    UiLocalizationEditor()
        : SourceDocumentPanel("UI localization", ui_localization_import_registry,
                              prepare_ui_localization_publication, desktop_texture_target()) {}
    bool has_unapplied_edits() const { return gesture_dirty(); }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New UI localization...", "Create a language and literal-message catalog."))
            ImGui::OpenPopup("New UI localization");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New UI localization", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create an editable catalog with an English default locale.")) {
                try {
                    auto created = UiLocalizationDocument::create(project.writer_guard(),
                                                                  std::filesystem::u8path(path_));
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a file."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("UI Localization###UI Localization", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close the failed source document."))
                        request_close();
                }
                ImGui::End();
            }
            return;
        }
        if (save_ && !locked && !pending()) {
            save_ = false;
            if (flush_gesture())
                publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("UI Localization###UI Localization", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("ui_localization",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped("Literal game UI messages. Missing translations use the default "
                               "language; message keys are shared by all languages.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Save and cook the catalog for Game and export."))
                request_save();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo source edits or cancel the current text draft."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo source edits."))
                redo();
            ImGui::EndDisabled();
            draw_catalog();
            ImGui::SeparatorText("Scene controller");
            ImGui::TextWrapped("One configured UI Localization controller serves this Scene's UI "
                               "documents. Locale is adjustable in the Inspector during Play.");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty() || gesture_dirty());
            if (ui::button("Attach to selected object",
                           "Assign this catalog as one Scene Undo step."))
                attach(scene);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing UI localization...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending localization changes");
        if (ImGui::BeginPopupModal("Pending localization changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save and publish, discard the draft, or keep editing?");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Publish before closing.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::button("Discard", "Close without saving this draft.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Keep editing", "Cancel closing.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    char path_[256] = "Assets/Messages.localization.json", locale_input_[36] = "de",
         key_input_[49] = "menu_play";
    std::array<char, 65> label_{};
    std::array<char, 1025> message_{};
    std::string locale_, key_;
    bool label_dirty_ = false, message_dirty_ = false;
    std::uint64_t text_revision_ = 0;
    ImGuiID label_input_id_ = 0, message_input_id_ = 0;
    void release_text_input() {
        if (ImGui::GetCurrentContext() && ImGui::GetActiveID() &&
            (ImGui::GetActiveID() == label_input_id_ || ImGui::GetActiveID() == message_input_id_))
            ImGui::ClearActiveID();
    }
    bool gesture_dirty() const override { return label_dirty_ || message_dirty_; }
    void cancel_gesture() override {
        release_text_input();
        label_dirty_ = message_dirty_ = false;
        reconcile();
    }
    bool flush_gesture() override {
        if (!gesture_dirty())
            return true;
        const auto locale = locale_, key = key_, label = std::string(label_.data()),
                   message = std::string(message_.data());
        const bool change_label = label_dirty_, change_message = message_dirty_;
        return edit_source(text_revision_, "Edit localized text", [=](Json& d) {
            auto& language = d.at("locales").at(locale);
            if (change_label)
                language["label"] = label;
            if (change_message && !key.empty())
                language["messages"][key] = message;
        });
    }
    void view_closed() override {
        locale_.clear();
        key_.clear();
    }
    void source_opened() override { reconcile(); }
    void changed() override {
        release_text_input();
        needs_publish_ = true;
        label_dirty_ = message_dirty_ = false;
        reconcile();
    }
    void reconcile() {
        if (!document_)
            return;
        const auto& d = document_->source().document;
        const auto& locales = d.at("locales");
        if (!locales.contains(locale_))
            locale_ = d.at("default_locale");
        const auto& keys = locales.at(d.at("default_locale").get<std::string>()).at("messages");
        if (!keys.contains(key_))
            key_ = keys.empty() ? std::string{} : keys.begin().key();
        const auto& language = locales.at(locale_);
        const auto label = language.at("label").get<std::string>();
        const auto text =
            key_.empty() ? std::string{} : language.at("messages").value(key_, std::string{});
        std::snprintf(label_.data(), label_.size(), "%s", label.c_str());
        std::snprintf(message_.data(), message_.size(), "%s", text.c_str());
    }
    template <class F> bool mutate(std::string label, F edit) {
        return flush_gesture() && edit_source(document_->revision(), std::move(label), edit);
    }
    void text_changed(bool label) {
        if (!gesture_dirty())
            text_revision_ = document_->revision();
        (label ? label_dirty_ : message_dirty_) = true;
    }
    void draw_catalog() {
        const auto source = document_->source().document;
        const auto default_locale = source.at("default_locale").get<std::string>();
        ImGui::SeparatorText("Languages");
        if (ImGui::BeginCombo("Locale", locale_.c_str())) {
            for (const auto& [tag, language] : source.at("locales").items()) {
                const auto caption = tag + " - " + language.at("label").get<std::string>();
                if (ImGui::Selectable(caption.c_str(), tag == locale_) && flush_gesture()) {
                    locale_ = tag;
                    reconcile();
                }
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("ui_localization:locale");
        ImGui::Text("Default locale: %s", default_locale.c_str());
        if (ImGui::InputText("Language label", label_.data(), label_.size()))
            text_changed(true);
        label_input_id_ = ImGui::GetItemID();
        FORGE_UI_PROBE("ui_localization:label");
        if (ImGui::IsItemDeactivatedAfterEdit())
            flush_gesture();
        ImGui::BeginDisabled(locale_ == default_locale);
        bool languages_changed = false;
        if (ui::button("Make default",
                       "Copy missing default messages, then select this language as default."))
            languages_changed = mutate("Choose default UI locale", [&](Json& d) {
                auto& messages = d["locales"][locale_]["messages"];
                for (const auto& [key, value] : d["locales"][default_locale]["messages"].items())
                    if (!messages.contains(key))
                        messages[key] = value;
                d["default_locale"] = locale_;
            });
        ImGui::SameLine();
        if (ui::button("Remove language", "Remove this non-default language and its translations."))
            languages_changed =
                mutate("Remove UI language", [&](Json& d) { d["locales"].erase(locale_); });
        ImGui::EndDisabled();
        if (languages_changed)
            return;
        ImGui::InputText("New locale tag", locale_input_, sizeof(locale_input_));
        FORGE_UI_PROBE("ui_localization:new_locale");
        ImGui::BeginDisabled(source.at("locales").size() >= 8);
        if (ui::button("Add language", "Add an ASCII language tag, for example fr or en-GB.")) {
            const auto tag = std::string(locale_input_);
            if (mutate("Add UI language", [&](Json& d) {
                    if (d["locales"].contains(tag))
                        throw std::runtime_error("This locale already exists");
                    d["locales"][tag] = {{"label", tag}, {"messages", Json::object()}};
                })) {
                locale_ = tag;
                reconcile();
                ImGui::EndDisabled();
                return;
            }
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Messages");
        if (ImGui::BeginCombo("Message", key_.empty() ? "No messages" : key_.c_str())) {
            for (const auto& [key, value] :
                 source.at("locales").at(default_locale).at("messages").items())
                if (ImGui::Selectable(key.c_str(), key == key_) && flush_gesture()) {
                    key_ = key;
                    reconcile();
                }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("ui_localization:key");
        ImGui::InputText("New / renamed key", key_input_, sizeof(key_input_));
        FORGE_UI_PROBE("ui_localization:new_key");
        if (ui::button("Add message",
                       "Create a key in the default language; enter its text below.")) {
            const auto key = std::string(key_input_);
            if (mutate("Add UI message", [&](Json& d) {
                    auto& messages = d["locales"][default_locale]["messages"];
                    if (messages.contains(key))
                        throw std::runtime_error("This message key already exists");
                    messages[key] = "";
                })) {
                key_ = key;
                locale_ = default_locale;
                reconcile();
                return;
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(key_.empty());
        if (ui::button("Rename key",
                       "Rename in every language. Update calling RML expressions yourself.")) {
            const auto key = std::string(key_input_);
            if (mutate("Rename UI message", [&](Json& d) {
                    if (d["locales"][default_locale]["messages"].contains(key))
                        throw std::runtime_error("This message key already exists");
                    for (auto& language : d["locales"])
                        if (auto& messages = language["messages"]; messages.contains(key_)) {
                            messages[key] = messages.at(key_);
                            messages.erase(key_);
                        }
                })) {
                key_ = key;
                reconcile();
                ImGui::EndDisabled();
                return;
            }
        }
        ImGui::SameLine();
        if (ui::button("Remove message",
                       "Remove this key and its translations in every language.") &&
            mutate("Remove UI message", [&](Json& d) {
                for (auto& language : d["locales"])
                    language["messages"].erase(key_);
            })) {
            ImGui::EndDisabled();
            return;
        }
        if (!key_.empty()) {
            const auto text =
                source.at("locales").at(default_locale).at("messages").at(key_).get<std::string>();
            if (locale_ != default_locale) {
                ImGui::TextWrapped("Default: %s", text.c_str());
                if (!source.at("locales").at(locale_).at("messages").contains(key_))
                    ImGui::TextWrapped("No translation: the default message is displayed in Game.");
            }
            if (ImGui::InputTextMultiline("Text", message_.data(), message_.size(),
                                          ImVec2(ImGui::GetContentRegionAvail().x * .85f,
                                                 5 * ImGui::GetTextLineHeightWithSpacing())))
                text_changed(false);
            message_input_id_ = ImGui::GetItemID();
            FORGE_UI_PROBE("ui_localization:text");
            if (ImGui::IsItemDeactivatedAfterEdit())
                flush_gesture();
            ImGui::BeginDisabled(locale_ == default_locale);
            if (ui::button("Use default text",
                           "Remove this translation so the default language supplies it."))
                mutate("Use default UI message",
                       [&](Json& d) { d["locales"][locale_]["messages"].erase(key_); });
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped(
            "RML: {{ 'message_key' | tr(forge_locale) }}. Messages are literal text; "
            "markup, plural rules and parameter formatting are not interpreted.");
    }
    void attach(Scene& scene) {
        try {
            const auto id =
                ui::editor_context ? ui::editor_context->selection.entity() : std::string{};
            const auto actor = scene.entity(id);
            if (!actor)
                throw std::runtime_error("Choose a Scene object for the localization controller");
            for (const auto& row : scene.ui_document_rows()) {
                const auto& c = row.at("components");
                if (!row.value("prefab", false) && row.at("id") != id &&
                    c.contains("forge.ui_localization") &&
                    !c.at("forge.ui_localization").at("catalog").is_null())
                    throw std::runtime_error(
                        "This Scene already has a configured UI Localization controller");
            }
            auto commands = Json::array();
            if (!actor.has<UiLocalization>())
                commands.push_back(
                    {{"operation", "component.add"},
                     {"arguments", {{"entity", id}, {"component", "forge.ui_localization"}}}});
            commands.push_back({{"operation", "property.set"},
                                {"arguments",
                                 {{"entity", id},
                                  {"component", "forge.ui_localization"},
                                  {"field", "catalog"},
                                  {"value", document_->source().asset()}}}});
            commands.push_back({{"operation", "property.set"},
                                {"arguments",
                                 {{"entity", id},
                                  {"component", "forge.ui_localization"},
                                  {"field", "locale"},
                                  {"value", document_->source().document.at("default_locale")}}}});
            apply_authoring(scene, commands, scene.revision());
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
