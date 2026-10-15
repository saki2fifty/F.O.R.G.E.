#pragma once
#include "animation_material_inputs.hpp"
#include "animation_material_patch.hpp"
#include "animation_material_values.hpp"
#include "material_parameter_overrides.hpp"
#include "transform_animation_preview.hpp"
#include <forge/runtime_resources.hpp>
#include <forge/scene.hpp>
namespace forge {
// Isolated CPU resource consumer for an unsaved Timeline source. The existing
// Timeline still owns its clock/history; the existing Resources service owns
// loading/adoption. No world component or authored material is changed here.
class AnimationMaterialPreview {
  public:
    void reset() {
        ready_.reset();
        identity_.reset();
        inputs_.clear();
        engine_.reset();
        project_.clear();
    }
    void refresh() {
        if (engine_)
            engine_->services().resources()->refresh();
        identity_.reset();
    }
    bool prepare(const std::filesystem::path& project, const MeshRenderer& renderer,
                 const TransformAnimationClip& clip, std::uint64_t revision) {
        if (!engine_ || project != project_) {
            reset();
            engine_ = std::make_unique<EngineContext>(
                WorldRole::Runtime, false, std::vector{runtime_resources_module(project)});
            project_ = project;
        }
        const auto resources = engine_->services().resources();
        resources->synchronize(); // Explicit private preview preparation boundary.
        inputs_.begin(resources);
        asset_detail::PreparedModelDraw draw;
        try {
            draw = inputs_.select(renderer);
        } catch (const AnimationMaterialPending&) {
            inputs_.finish();
            ready_.reset();
            identity_.reset();
            return false;
        } catch (...) {
            inputs_.finish();
            ready_.reset();
            identity_.reset();
            throw;
        }
        inputs_.finish();
        auto identity = asset_detail::prepared_model_draw_key(draw, false);
        if (!identity_ || *identity_ != identity || revision_ != revision ||
            renderer_ != renderer) {
            ready_.reset();
            identity_.reset();
            (void)asset_detail::prepare_model_material_parameters(draw,
                                                                  renderer.material_parameters);
            validate_animation_material_target(clip, draw, renderer.material_parameters);
            renderer_ = renderer;
            revision_ = revision;
            identity_ = std::move(identity);
            ready_ = std::move(draw);
        }
        return true;
    }
    const asset_detail::PreparedModelDraw* ready() const { return ready_ ? &*ready_ : nullptr; }
    MaterialParameter current(const AnimationMaterialBinding& binding,
                              const MeshRenderer& renderer) const {
        for (const auto& value : renderer.material_parameters)
            if (value.slot == binding.slot && value.key == binding.key) {
                if (value.parameter.type != binding.type)
                    throw std::runtime_error("Captured material parameter semantic type differs");
                return value.parameter;
            }
        if (!ready_)
            throw AnimationMaterialPending("Waiting for material capture definitions");
        for (const auto& slot : ready_->selection.bindings) {
            if (slot.key != binding.slot)
                continue;
            for (std::size_t l = 0; l < ready_->mesh->mesh.lods.size(); ++l)
                for (std::size_t p = 0; p < ready_->mesh->mesh.lods[l].parts.size(); ++p) {
                    if (ready_->mesh->mesh.lods[l].parts[p].material_slot != slot.physical_slot)
                        continue;
                    const auto asset = ready_->selection.parts.at(l).at(p).id;
                    MaterialResourceData fallback;
                    fallback.values.model = "forge.gltf.metallic-roughness.v1";
                    const auto& material = asset ? ready_->materials.at(asset).get() : fallback;
                    const auto* surface =
                        material.surface ? &*material.surface->program.surface : nullptr;
                    const auto values = detail::project_material_parameters(
                        material.values, surface, binding.slot, {});
                    const auto found = values.parameters.find(binding.key);
                    if (found == values.parameters.end() || found->second.type != binding.type)
                        throw std::runtime_error("Captured material parameter declaration differs");
                    return found->second;
                }
        }
        throw std::runtime_error("Captured material slot is unavailable");
    }

  private:
    std::filesystem::path project_;
    std::unique_ptr<EngineContext> engine_;
    AnimationMaterialInputs inputs_;
    std::optional<asset_detail::PreparedModelDraw> ready_;
    std::optional<asset_detail::PreparedModelDrawKey> identity_;
    MeshRenderer renderer_;
    std::uint64_t revision_ = 0;
};
// Preflight both property families on detached component values. Only then use
// the existing transform/property preview operation and move the prepared numeric
// renderer into the same copied row. Source/history and native components stay intact.
inline bool apply_material_animation_preview(Json& document, EntityRef target,
                                             const TransformAnimationClip& clip, double time,
                                             const asset_detail::PreparedModelDraw* draw) {
    if (clip.material_bindings.empty())
        return apply_transform_animation_preview(document, target, clip, time);
    if (document.at("asset_id") != Json(target.scene))
        return false;
    auto row =
        std::find_if(document.at("entities").begin(), document.at("entities").end(),
                     [&](const auto& value) { return value.at("id") == target.entity.str(); });
    if (row == document.at("entities").end())
        return false;
    const auto& components = row->at("components");
    if (!components.contains("forge.mesh_renderer"))
        throw std::runtime_error("Material preview requires a MeshRenderer component");
    if (!draw || !draw->mesh)
        throw AnimationMaterialPending("Preparing selected material preview resources");
    auto renderer = components.at("forge.mesh_renderer");
    detail::validate_components({{"forge.mesh_renderer", renderer}});
    if (renderer.at("mesh") != Json(draw->mesh.identity().asset))
        throw AnimationMaterialPending("Material preview Mesh selection changed");
    std::vector<MaterialSlotOverride> assignments;
    for (const auto& value : renderer.at("materials"))
        assignments.push_back(
            {value.at("slot").template get<std::string>(),
             {value.at("material").is_null() ? AssetId{}
                                             : value.at("material").template get<AssetId>()}});
    AssetRef<MaterialVariantAsset> variant;
    if (renderer.contains("material_variant") && !renderer.at("material_variant").is_null())
        variant = {renderer.at("material_variant").template get<AssetId>()};
    auto selected = *draw;
    selected.selection = select_mesh_materials(draw->mesh.get(), assignments, variant);
    if (asset_detail::prepared_model_draw_key(selected, false) !=
        asset_detail::prepared_model_draw_key(*draw, false))
        throw AnimationMaterialPending("Material preview slot selection changed");
    std::vector<MaterialParameterOverride> current;
    if (renderer.contains("material_parameters"))
        for (const auto& value : renderer.at("material_parameters"))
            current.push_back(
                {value.at("slot").template get<std::string>(),
                 value.at("key").template get<std::string>(),
                 {MaterialParameterType(value.at("parameter").at("type").template get<int>()),
                  value.at("parameter").at("value").template get<std::array<float, 4>>()}});
    const auto sampled = sample_animation_material_parameters(clip, time, current);
    (void)asset_detail::prepare_model_material_parameters(*draw, sampled);
    merge_animation_material_parameters(renderer, animation_material_parameters_json(sampled));
    if (!apply_transform_animation_preview(document, target, clip, time))
        return false;
    row->at("components").at("forge.mesh_renderer") = std::move(renderer);
    return true;
}
} // namespace forge
