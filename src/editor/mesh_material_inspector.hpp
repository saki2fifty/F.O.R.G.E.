#pragma once
#include "../model_render_resource.hpp"
#include "property_drawer.hpp"
#include <set>
namespace forge {
// A temporary selection consumer, not an asset registry. Only copied logical slot
// bindings survive CPU preparation; the mesh payload is released immediately.
class MeshMaterialInspector {
  public:
    void select(const std::filesystem::path& project, std::shared_ptr<const AssetCatalog> catalog,
                AssetRef<MeshAsset> mesh) {
        pool_.pump();
        if (project == project_ && catalog == catalog_ && mesh == mesh_)
            return;
        if (mesh_.id)
            pool_.unload(mesh_);
        pool_.collect();
        ticket_ = {};
        bindings_.clear();
        error_.clear();
        project_ = project;
        catalog_ = std::move(catalog);
        mesh_ = mesh;
        if (!mesh.id || !catalog_)
            return;
        try {
            ticket_ = asset_detail::request_model_mesh(pool_, project_, catalog_, mesh);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    bool pending() const { return ticket_.has_value(); }
    const std::vector<MeshMaterialBinding>& bindings() const { return bindings_; }
    const std::string& error() const { return error_; }
    void poll() {
        pool_.pump();
        if (!ticket_)
            return;
        const auto info = ticket_->inspect();
        if (info.state == ResourceState::Ready) {
            {
                auto lease = pool_.acquire(*ticket_);
                if (!lease)
                    throw std::runtime_error("Inspector mesh selection expired");
                bindings_ = lease->materials;
            }
            pool_.unload(mesh_);
            pool_.collect();
            ticket_.reset();
        } else if (resource_detail::terminal(info.state)) {
            error_ = info.diagnostic.empty() ? resource_state_name(info.state) : info.diagnostic;
            ticket_.reset();
        }
    }
    bool draw(Json& overrides) {
        poll();
        ui::property_label_row("Materials",
                               "Assign materials by stable mesh slot. Changes belong "
                               "to this scene; shared material sources are unchanged.");
        if (pending())
            ImGui::TextDisabled("Loading mesh slots...");
        if (!error_.empty()) {
            ui::field_error(error_);
            if (ui::button("Retry mesh slots", "Retry loading the selected cooked mesh. No scene "
                                               "or material source changes.")) {
                auto catalog = catalog_;
                catalog_.reset();
                select(project_, std::move(catalog), mesh_);
            }
        }
        if (!catalog_)
            return false;
        std::set<std::string> known;
        for (const auto& slot : bindings_) {
            known.insert(slot.key);
            ui::IdScope scope(slot.key.c_str());
            auto entry = std::find_if(overrides.begin(), overrides.end(),
                                      [&](const Json& v) { return v.at("slot") == slot.key; });
            const bool owned = entry != overrides.end();
            Json value = owned ? entry->at("material")
                               : (slot.material.id ? Json(slot.material.id) : Json(nullptr));
            const auto label = (slot.key == "default" || slot.key == "surface")
                                   ? "Surface"
                                   : "Surface " + slot.key;
            if (asset_ref_picker(*catalog_, value, MaterialAsset::type, label.c_str())) {
                if (owned)
                    (*entry)["material"] = value;
                else
                    overrides.push_back({{"slot", slot.key}, {"material", value}});
                return true;
            }
            ImGui::TextDisabled("%s", owned ? "Scene assignment" : "Mesh default");
            ui::help("An explicit assignment remains even when it equals the mesh default. "
                     "None is an explicit empty assignment. Use Mesh default to remove it.");
            if (owned && ui::button("Use mesh default", "Remove this slot assignment. Other slots "
                                                        "are unchanged. One scene Undo step.")) {
                overrides.erase(entry);
                return true;
            }
        }
        // Reimport may remove a key. Keep the intent visible and removable; never
        // silently reassign it to a numerically similar physical slot.
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            const auto key = it->at("slot").get<std::string>();
            if (known.contains(key))
                continue;
            ui::IdScope scope(key.c_str());
            ImGui::TextWrapped("Unresolved slot: %s", key.c_str());
            ui::help("This authored slot has no current mesh binding, or its mesh has not loaded. "
                     "It is preserved across reimport and scene saves.");
            auto value = it->at("material");
            if (asset_ref_picker(*catalog_, value, MaterialAsset::type, "Material")) {
                (*it)["material"] = value;
                return true;
            }
            if (ui::button("Remove assignment", "Remove only this unresolved slot assignment. "
                                                "Scene Undo restores it.")) {
                overrides.erase(it);
                return true;
            }
        }
        if (!mesh_.id)
            ImGui::TextWrapped("Choose a Mesh to see its material slots.");
        return false;
    }

  private:
    ResourcePool<MeshAsset> pool_{{1, 4, 64, 512ull * 1024 * 1024}};
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    AssetRef<MeshAsset> mesh_;
    std::optional<ResourceTicket> ticket_;
    std::vector<MeshMaterialBinding> bindings_;
    std::string error_;
};
} // namespace forge
