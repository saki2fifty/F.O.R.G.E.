#pragma once
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/model_asset.hpp>
namespace forge::ui {
#ifdef FORGE_UI_FIXTURE
inline bool fixture_open_model_variant = false;
#endif
inline void model_variant_controls(Scene& scene, SceneDocument& project, const std::string& entity,
                                   const Json& source, std::string& error) {
    if (!source.at("node").is_null() || source.at("model").is_null())
        return;
    const auto owner = source.at("model").get<AssetId>();
#ifdef FORGE_UI_FIXTURE
    if (fixture_open_model_variant) {
        ImGui::SetScrollHereY(.5f);
        ImGui::OpenPopup("Model material variants");
    }
#endif
    if (button("Set model variant...",
               "Choose one material set for all mesh nodes in this placed model. One scene Undo "
               "step; explicit material slot overrides are preserved."))
        ImGui::OpenPopup("Model material variants");
    if (ImGui::BeginPopup("Model material variants")) {
#ifdef FORGE_UI_FIXTURE
        fixture_open_model_variant = false;
#endif
        try {
            const auto catalog = AssetCatalog::open_project(project.project());
            const auto apply = [&](AssetId variant) {
                project.check_ownership();
                authoring_command(
                    scene, "model.material_variant",
                    {{"entity", entity}, {"variant", variant ? Json(variant) : Json()}});
                error.clear();
            };
            if (ImGui::MenuItem("Default materials"))
                apply({});
            help("Clear variant selection on every mesh node. Explicit per-slot material "
                 "assignments remain in effect.");
            unsigned count = 0;
            for (const auto& [id, record] : catalog.records()) {
                if (record.type != MaterialVariantAsset::type || !record.subasset ||
                    record.subasset->owner != owner || record.subasset->removed)
                    continue;
                ++count;
                IdScope scope(id.str().c_str());
                const auto label = record.metadata.at("forge.model")
                                       .value("name", std::string("Material variant"));
                if (ImGui::MenuItem(label.empty() ? "Unnamed variant" : label.c_str()))
                    apply(id);
                help(("Imported material set: " + id.str() +
                      ". Source files and nested model instances stay unchanged.")
                         .c_str());
            }
            if (!count)
                ImGui::TextDisabled("No imported material variants.");
        } catch (const std::exception& e) {
            error = e.what();
        }
        ImGui::EndPopup();
    }
    if (!error.empty())
        field_error(error);
}
} // namespace forge::ui
