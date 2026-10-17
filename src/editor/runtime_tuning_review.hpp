#pragma once
#include "../json_value_equal.hpp"
#include "component_inspector.hpp"

namespace forge {
// Session-only review over the borrowed Scene owner. Runtime snapshots never
// become authored documents; selected fields use the ordinary authoring batch.
class RuntimeTuningReview {
  public:
    struct Field {
        std::string component, property, label, before_text, after_text;
        Json before, after;
        bool selected = true;
    };
    bool open = false;
    bool pending() const { return !fields_.empty(); }
    const std::vector<Field>& fields() const { return fields_; }
    const std::string& status() const { return status_; }
    void select(std::size_t index, bool selected) { fields_.at(index).selected = selected; }
    void discard() {
        fields_.clear();
        types_ = Json::object();
        status_.clear();
        open = false;
        if (ui::editor_context)
            ui::editor_context->problems.resolve("runtime.review");
    }
    bool capture(Scene& scene, SceneDocument& document, const Json& runtime,
                 const Json& runtime_schema, const std::string& entity, std::uint64_t tick) {
        open = true;
        try {
            if (pending())
                throw std::runtime_error(
                    "Apply or discard the existing review before capturing another.");
            document.check_ownership();
            if (runtime.at("asset_id") != Json(scene.asset_id()))
                throw std::runtime_error("The runtime snapshot belongs to another Scene.");
            const auto authored = scene.effective_document();
            const auto* source = find(authored.at("entities"), entity);
            const auto* live = find(runtime.at("entities"), entity);
            if (!source || !live)
                throw std::runtime_error("Choose an entity that exists in the authored Scene.");
            const auto schema = scene.schema();
            const auto& current = source->at("components");
            const auto& values = live->at("components");
            std::vector<Field> next;
            Json types = Json::object();
            std::size_t bytes = 0;
            for (const auto& type : runtime_schema.at("components")) {
                const auto key = type.at("id").get<std::string>();
                const auto* admitted = find(schema.at("components"), key);
                if (!admitted || !current.contains(key) ||
                    !ComponentInspector::runtime_type_available(type, values) ||
                    !ComponentInspector::runtime_type_available(*admitted, current))
                    continue;
                if (admitted->value("custom", false) &&
                    admitted->at("admission") != type.at("admission"))
                    throw std::runtime_error("Runtime and authored component schemas differ.");
                for (const auto& field : admitted->at("fields")) {
                    const auto name = field.at("id").get<std::string>();
                    const auto* live_field = find(type.at("fields"), name);
                    if (!live_field || live_field->value("read_only", false) ||
                        field.value("read_only", false) || !current.at(key).contains(name) ||
                        !ComponentInspector::runtime_field_available(key, field, values) ||
                        detail::json_value_equal(current.at(key).at(name), values.at(key).at(name)))
                        continue;
                    Field row{key,
                              name,
                              admitted->value("display_name", key) + " / " +
                                  field.value("display_name", name),
                              current.at(key).at(name).dump(),
                              values.at(key).at(name).dump(),
                              current.at(key).at(name),
                              values.at(key).at(name)};
                    bytes += row.before_text.size() + row.after_text.size();
                    if (next.size() == 128 || bytes > 1024 * 1024)
                        throw std::runtime_error("Review exceeds 128 fields or 1 MiB of values.");
                    next.push_back(std::move(row));
                    types[key] = *admitted;
                }
            }
            if (next.empty())
                throw std::runtime_error(
                    "No changed tunable values are available for this entity.");
            project_ = document.project();
            generation_ = document.generation();
            revision_ = scene.revision();
            asset_ = scene.asset_id();
            entity_ = entity;
            name_ = source->value("name", entity);
            source_label_ =
                document.name() + " / " +
                (document.path().empty() ? "Untitled" : path_utf8(document.path().filename()));
            tick_ = tick;
            types_ = std::move(types);
            fields_ = std::move(next);
            status_ = "Snapshot captured. Stop Play, choose fields, then Apply to Scene.";
            return true;
        } catch (const std::exception& error) {
            status_ = error.what();
            return false;
        }
    }
    bool apply(Scene& scene, SceneDocument& document, bool locked) {
        try {
            if (locked)
                throw std::runtime_error("Stop Play and finish the active operation first.");
            if (!pending())
                throw std::runtime_error("There is no runtime tuning review to apply.");
            document.check_ownership();
            if (const auto why = stale(scene, document); !why.empty())
                throw std::runtime_error(why);
            const auto schema = scene.schema();
            for (const auto& [key, type] : types_.items()) {
                const auto* current = find(schema.at("components"), key);
                if (!current || !detail::json_value_equal(*current, type))
                    throw std::runtime_error(
                        "Component schema changed. Discard and capture a new review.");
            }
            Json grouped = Json::object();
            for (const auto& field : fields_)
                if (field.selected)
                    grouped[field.component][field.property] = field.after;
            if (grouped.empty())
                throw std::runtime_error("Choose at least one field to apply.");
            Json commands = Json::array();
            for (const auto& [type, values] : grouped.items())
                commands.push_back(
                    {{"operation", "property.set_many"},
                     {"arguments",
                      {{"entity", entity_}, {"component", type}, {"values", values}}}});
            // The ordinary batch validates its complete detached candidates before
            // publishing once. No second serializer, Scene writer or history owner.
            apply_authoring(scene, commands, revision_);
            discard();
            status_ = "Selected runtime values applied. Scene Save and Undo are available.";
            return true;
        } catch (const std::exception& error) {
            status_ = error.what();
            return false;
        }
    }
    bool draw(Scene& scene, SceneDocument& document, bool locked) {
        if (!open)
            return false;
        bool applied = false;
        ui::draft_window_size({900 * ui::interface_scale, 620 * ui::interface_scale});
        if (ImGui::Begin("Keep runtime values", &open)) {
            if (ui::editor_context)
                ui::editor_context->task.focus(ui::DocumentTask::Scene);
            if (pending())
                ImGui::TextWrapped("%s\n%s — frozen runtime snapshot at tick %llu",
                                   source_label_.c_str(), name_.c_str(),
                                   static_cast<unsigned long long>(tick_));
            else
                ImGui::TextUnformatted("No runtime values staged.");
            ImGui::TextWrapped("Choose values to keep in the Scene. Later gameplay changes are "
                               "not included. Apply creates one Undo step; Save is separate.");
            const auto why = pending() ? stale(scene, document) : std::string{};
            if (!why.empty())
                ImGui::TextWrapped("%s", why.c_str());
            ImGui::BeginDisabled(locked || !pending() || !why.empty());
            if (ui::button("Apply to Scene",
                           "Validate and apply the selected frozen values after Stop.")) {
                applied = apply(scene, document, locked);
                if (!applied)
                    ui::report_error("runtime.review", status_);
            }
            FORGE_UI_PROBE("runtime_review:apply");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Discard review", "Forget this snapshot without changing the Scene."))
                discard();
            FORGE_UI_PROBE("runtime_review:discard");
            ImGui::SameLine();
            if (ui::button("Close review", "Keep this session's review; reopen it from Run."))
                open = false;
            FORGE_UI_PROBE("runtime_review:close");
            if (locked)
                ImGui::TextWrapped("Stop Play and finish the active operation before applying.");
            if (!status_.empty())
                ImGui::TextWrapped("%s", status_.c_str());
            if (ImGui::BeginTable(
                    "##runtime review", 4,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                    {0, std::max(80.f * ui::interface_scale, ImGui::GetContentRegionAvail().y)})) {
                ImGui::TableSetupColumn("Keep", ImGuiTableColumnFlags_WidthFixed,
                                        45 * ui::interface_scale);
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 3);
                ImGui::TableSetupColumn("Scene value", ImGuiTableColumnFlags_WidthStretch, 2);
                ImGui::TableSetupColumn("Runtime value", ImGuiTableColumnFlags_WidthStretch, 2);
                ImGui::TableHeadersRow();
                for (std::size_t index = 0; index < fields_.size(); ++index) {
                    auto& row = fields_[index];
                    ui::IdScope id(std::to_string(index).c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("##keep", &row.selected);
                    FORGE_UI_PROBE("runtime_review:field:" + row.component + ":" + row.property);
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", row.label.c_str());
                    if (row.before_text.size() > 160 || row.after_text.size() > 160) {
                        if (ui::button("Details...",
                                       "Read the complete frozen before/after values."))
                            ImGui::OpenPopup("Value details");
                        FORGE_UI_PROBE("runtime_review:details:" + row.component + ":" +
                                       row.property);
                        ui::draft_window_size(
                            {600 * ui::interface_scale, 380 * ui::interface_scale});
                        if (ImGui::BeginPopup("Value details")) {
                            ImGui::TextWrapped("Scene: %s", row.before_text.c_str());
                            ImGui::Separator();
                            ImGui::TextWrapped("Runtime: %s", row.after_text.c_str());
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", row.before_text.size() > 160
                                                 ? "See Details..."
                                                 : row.before_text.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", row.after_text.size() > 160 ? "See Details..."
                                                                         : row.after_text.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
        return applied;
    }

  private:
    std::vector<Field> fields_;
    Json types_ = Json::object();
    std::filesystem::path project_;
    AssetId asset_;
    std::uint64_t generation_ = 0, revision_ = 0, tick_ = 0;
    std::string entity_, name_, source_label_, status_;
    static const Json* find(const Json& rows, const std::string& id) {
        for (const auto& row : rows)
            if (row.value("id", "") == id)
                return &row;
        return nullptr;
    }
    std::string stale(const Scene& scene, const SceneDocument& document) const {
        if (document.project() != project_ || document.generation() != generation_ ||
            scene.asset_id() != asset_ || scene.revision() != revision_)
            return "Authored Scene changed or was reopened. Discard and capture a new review.";
        return {};
    }
};
} // namespace forge
