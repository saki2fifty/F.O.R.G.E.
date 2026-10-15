#pragma once
#include "../material_parameter_overrides.hpp"
#include "../model_render_resource.hpp"
#include "../pbr_material.hpp"
#include "material_labels.hpp"
#include "property_drawer.hpp"
#include <set>
namespace forge {
// A temporary selection consumer, not an asset registry. Only copied logical slot
// bindings survive CPU preparation; the mesh payload is released immediately.
class MeshMaterialInspector {
  public:
    void select(const std::filesystem::path& project, std::shared_ptr<const AssetCatalog> catalog,
                AssetRef<MeshAsset> mesh, std::vector<MaterialSlotOverride> assignments = {},
                AssetRef<MaterialVariantAsset> variant = {}) {
        pool_.pump();
        if (project == project_ && catalog == catalog_ && mesh == mesh_ &&
            assignments == assignments_ && variant == variant_)
            return;
        if (mesh_.id)
            pool_.unload(mesh_);
        pool_.collect();
        ticket_ = {};
        bindings_.clear();
        selections_.clear();
        clear_parameters();
        error_.clear();
        project_ = project;
        catalog_ = std::move(catalog);
        mesh_ = mesh;
        assignments_ = std::move(assignments);
        variant_ = variant;
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
            try {
                auto lease = pool_.acquire(*ticket_);
                if (!lease)
                    throw std::runtime_error("Inspector mesh selection expired");
                bindings_ = lease->materials;
                const auto selection = select_mesh_materials(lease.get(), assignments_, variant_);
                std::map<std::uint32_t, std::string> keys;
                for (const auto& binding : selection.bindings)
                    keys.emplace(binding.physical_slot, binding.key);
                for (std::size_t l = 0; l < lease->mesh.lods.size(); ++l)
                    for (std::size_t p = 0; p < lease->mesh.lods[l].parts.size(); ++p) {
                        auto& materials =
                            selections_[keys.at(lease->mesh.lods[l].parts[p].material_slot)];
                        // A selected slot may span variant materials. Bound this
                        // temporary Inspector consumer independently of renderer support.
                        if (materials.size() <= parameter_material_limit)
                            materials.insert(selection.parts.at(l).at(p).id);
                    }
            } catch (const std::exception& error) {
                error_ = error.what();
                selections_.clear();
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
        ui::heading("Materials", "Assign materials by stable mesh slot. Changes belong "
                                 "to this scene; shared material sources are unchanged.");
        if (pending())
            ImGui::TextDisabled("Loading mesh slots...");
        if (!error_.empty()) {
            ui::field_error(error_);
            if (ui::button("Retry mesh slots", "Retry loading the selected cooked mesh. No scene "
                                               "or material source changes.")) {
                auto catalog = catalog_;
                catalog_.reset();
                select(project_, std::move(catalog), mesh_, assignments_, variant_);
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
            ui::property_label_row(label.c_str(), "Material assigned to this named mesh surface.");
            if (asset_ref_picker(*catalog_, value, MaterialAsset::type, "##material")) {
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
            ui::property_label_row("Material",
                                   "Preserved material assignment for this missing slot.");
            if (asset_ref_picker(*catalog_, value, MaterialAsset::type, "##material")) {
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
    // Uses this same selection consumer and the same reflected property command
    // as material slots. No authored material source or resource is mutated.
    bool draw_parameters(Json& values) {
        poll();
        ui::heading("Object material values", "Numeric values for this object. Other objects and "
                                              "shared material assets keep their own values.");
        if (!error_.empty())
            ui::field_error(error_);
        if (pending())
            ImGui::TextDisabled("Loading material slots...");
        if (!selections_.empty()) {
            if (!selections_.contains(parameter_slot_))
                parameter_slot_ = selections_.begin()->first;
            if (ImGui::BeginCombo("Material slot", parameter_slot_.c_str())) {
                for (const auto& [slot, selected] : selections_) {
                    (void)selected;
                    if (ImGui::Selectable(slot.c_str(), slot == parameter_slot_)) {
                        clear_parameters();
                        parameter_slot_ = slot;
                    }
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("object-material:slot");
            prepare_parameters();
        }
        poll_parameters();
        if (parameters_pending())
            ImGui::TextDisabled("Loading selected material parameters...");
        if (!parameter_error_.empty()) {
            ui::field_error(parameter_error_);
            if (!parameter_ready_ && !parameters_pending() &&
                ui::button("Retry parameters", "Retry loading the selected material definitions. "
                                               "Authored object values remain unchanged."))
                clear_parameters();
        }
        // Keep unavailable, removed and incompatible intent visible/removable.
        // A label, list order or physical material number never retargets a row.
        for (auto row = values.begin(); row != values.end(); ++row) {
            const auto slot = row->at("slot").get<std::string>();
            const auto key = row->at("key").get<std::string>();
            if (slot == parameter_slot_ && parameter_definitions_.contains(key))
                continue;
            ui::IdScope slot_id(slot.c_str());
            ui::IdScope key_id(key.c_str());
            ImGui::TextWrapped("Preserved override: %s / %s", slot.c_str(), key.c_str());
            ui::help("Select its slot to edit it. Unavailable keys and incompatible types remain "
                     "saved until explicitly removed.");
            if (ui::button("Remove override", "Remove this object value. Scene Undo restores its "
                                              "intent and extension data.")) {
                values.erase(row);
                parameter_error_.clear();
                return true;
            }
        }
        for (const auto& [key, default_value] : parameter_definitions_) {
            ui::IdScope id(key.c_str());
            const auto found = std::find_if(values.begin(), values.end(), [&](const auto& row) {
                return row.at("slot") == parameter_slot_ && row.at("key") == key;
            });
            const bool owned = found != values.end();
            auto parameter = default_value;
            if (owned) {
                parameter.type = static_cast<MaterialParameterType>(
                    found->at("parameter").at("type").template get<int>());
                parameter.value =
                    found->at("parameter").at("value").template get<std::array<float, 4>>();
            }
            const bool compatible = parameter.type == default_value.type;
            const auto& label = parameter_labels_.at(key);
            const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_None);
            FORGE_UI_PROBE("object-material:parameter:" + key);
            if (!open)
                continue;
            ImGui::TextDisabled("Binding: %s | %s", key.c_str(),
                                owned ? "Object override" : "Material default");
            if (!compatible)
                ui::field_error("Saved type differs from this material. Revert the override before "
                                "adding a replacement.");
            if (compatible) {
                const auto width = material_parameter_width(parameter.type);
                if (parameter.type == MaterialParameterType::LinearColor3 ||
                    parameter.type == MaterialParameterType::LinearColor4)
                    ImGui::TextDisabled("Linear color values");
                if (ImGui::InputScalarN("Value", ImGuiDataType_Float, parameter.value.data(),
                                        int(width), nullptr, nullptr, "%.6g",
                                        ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (commit_parameter(values, key, parameter)) {
                        ImGui::TreePop();
                        return true;
                    }
                }
                FORGE_UI_PROBE("object-material:value:" + key);
                ui::help(
                    "Enter commits one object value. Scalars, vectors and linear colors keep the "
                    "material's exact semantic type. Values outside its limits are rejected.");
            }
            if (owned) {
                if (ui::button("Revert to material",
                               "Remove this object's override and follow the selected material, "
                               "including future material changes.")) {
                    values.erase(found);
                    parameter_error_.clear();
                    ImGui::TreePop();
                    return true;
                }
                FORGE_UI_PROBE("object-material:revert:" + key);
            } else if (ui::button("Add override",
                                  "Explicitly use this value for this object. Equal-to-default "
                                  "override intent remains saved.")) {
                if (commit_parameter(values, key, parameter)) {
                    ImGui::TreePop();
                    return true;
                }
            }
            FORGE_UI_PROBE("object-material:add:" + key);
            ImGui::TreePop();
        }
        if (parameter_ready_ && parameter_definitions_.empty())
            ImGui::TextWrapped("No common adjustable numeric parameters in this slot.");
        ui::help("Texture choices, transparency mode and shader-feature changes belong to a "
                 "material variation. A slot spanning multiple variant materials exposes only "
                 "matching numeric declarations.");
        return false;
    }
    bool parameters_pending() const {
        return std::any_of(parameter_tickets_.begin(), parameter_tickets_.end(),
                           [](const auto& item) { return bool(item.second); });
    }
    // Also used by input tests; callers still publish the returned JSON through
    // their existing authored or live property transaction.
    bool commit_parameter(Json& values, const std::string& key, MaterialParameter parameter) {
        try {
            if (!parameter_ready_ || !parameter_definitions_.contains(key))
                throw std::runtime_error("Material parameters are not ready");
            auto candidate = values;
            const auto found =
                std::find_if(candidate.begin(), candidate.end(), [&](const auto& row) {
                    return row.at("slot") == parameter_slot_ && row.at("key") == key;
                });
            if (found == candidate.end())
                candidate.push_back(
                    {{"slot", parameter_slot_},
                     {"key", key},
                     {"parameter", {{"type", int(parameter.type)}, {"value", parameter.value}}}});
            else {
                (*found)["parameter"]["type"] = int(parameter.type);
                (*found)["parameter"]["value"] = parameter.value;
            }
            std::vector<MaterialParameterOverride> projected;
            for (const auto& row : candidate)
                projected.push_back(
                    {row.at("slot").template get<std::string>(),
                     row.at("key").template get<std::string>(),
                     {static_cast<MaterialParameterType>(
                          row.at("parameter").at("type").template get<int>()),
                      row.at("parameter").at("value").template get<std::array<float, 4>>()}});
            for (const auto& definition : definitions_)
                (void)detail::project_material_parameters(
                    definition.values, definition.surface ? &*definition.surface : nullptr,
                    parameter_slot_, projected);
            values = std::move(candidate);
            parameter_error_.clear();
            return true;
        } catch (const std::exception& error) {
            parameter_error_ = error.what();
            return false;
        }
    }

  private:
    static constexpr std::size_t parameter_material_limit = 16;
    struct Definition {
        MaterialData values;
        std::optional<SurfaceShaderDefinition> surface;
    };
    void clear_parameters() {
        for (const auto& [id, ticket] : parameter_tickets_) {
            (void)ticket;
            material_pool_.unload({id});
        }
        parameter_tickets_.clear();
        material_pool_.collect();
        definitions_.clear();
        parameter_definitions_.clear();
        parameter_labels_.clear();
        parameter_error_.clear();
        parameter_requested_ = parameter_ready_ = false;
    }
    void prepare_parameters() {
        if (parameter_requested_ || !selections_.contains(parameter_slot_))
            return;
        parameter_requested_ = true;
        const auto& selected = selections_.at(parameter_slot_);
        try {
            if (selected.size() > parameter_material_limit)
                throw std::runtime_error("This slot spans more than 16 materials. Assign one "
                                         "material to the slot to edit common values here.");
            for (const auto& id : selected) {
                if (id)
                    parameter_tickets_.emplace(id, asset_detail::request_model_pbr_material(
                                                       material_pool_, project_, catalog_, {id}));
                else {
                    MaterialData defaults;
                    defaults.model = "forge.gltf.metallic-roughness.v1";
                    definitions_.push_back({prepare_pbr_material(defaults).values, {}});
                }
            }
        } catch (const std::exception& error) {
            parameter_error_ = error.what();
        }
    }
    void poll_parameters() {
        material_pool_.pump();
        for (auto& [id, ticket] : parameter_tickets_) {
            if (!ticket)
                continue;
            const auto info = ticket.inspect();
            if (info.state == ResourceState::Ready) {
                {
                    const auto lease = material_pool_.acquire(ticket);
                    if (!lease) {
                        parameter_error_ = "Selected material expired before inspection";
                    } else {
                        Definition definition{lease->values, {}};
                        if (lease->surface)
                            definition.surface = lease->surface->program.surface;
                        else
                            definition.values = prepare_pbr_material(lease->values).values;
                        definitions_.push_back(std::move(definition));
                    }
                }
                material_pool_.unload({id});
                ticket = {};
            } else if (resource_detail::terminal(info.state)) {
                parameter_error_ =
                    info.diagnostic.empty() ? resource_state_name(info.state) : info.diagnostic;
                ticket = {};
            }
        }
        material_pool_.collect();
        if (!parameter_requested_ || parameter_ready_ || parameters_pending() ||
            !parameter_error_.empty() || definitions_.empty())
            return;
        const auto& first = definitions_.front();
        for (const auto& [key, parameter] : first.values.parameters) {
            if (!std::all_of(definitions_.begin(), definitions_.end(), [&](const auto& definition) {
                    const auto found = definition.values.parameters.find(key);
                    return found != definition.values.parameters.end() &&
                           found->second.type == parameter.type &&
                           (definition.surface ||
                            material_parameter_visible(definition.values.model, key)) &&
                           detail::object_material_parameter_supported(
                               key, definition.surface ? &*definition.surface : nullptr);
                }))
                continue;
            parameter_definitions_.emplace(key, parameter);
            std::string label = material_field_label(key);
            if (first.surface)
                if (const auto found = first.surface->labels.find(key);
                    found != first.surface->labels.end())
                    label = found->second;
            parameter_labels_.emplace(key, std::move(label));
        }
        parameter_ready_ = true;
    }
    ResourcePool<MeshAsset> pool_{{1, 4, 64, 512ull * 1024 * 1024}};
    ResourcePool<MaterialAsset> material_pool_{{2, 16, 64, 128ull * 1024 * 1024}};
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    AssetRef<MeshAsset> mesh_;
    std::optional<ResourceTicket> ticket_;
    std::vector<MeshMaterialBinding> bindings_;
    std::vector<MaterialSlotOverride> assignments_;
    AssetRef<MaterialVariantAsset> variant_;
    std::map<std::string, std::set<AssetId>> selections_;
    std::map<AssetId, ResourceTicket> parameter_tickets_;
    std::vector<Definition> definitions_;
    std::string parameter_slot_, parameter_error_;
    std::map<std::string, MaterialParameter> parameter_definitions_;
    std::map<std::string, std::string> parameter_labels_;
    bool parameter_requested_ = false, parameter_ready_ = false;
    std::string error_;
};
} // namespace forge
