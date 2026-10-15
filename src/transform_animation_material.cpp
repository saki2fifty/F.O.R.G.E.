#include "animation_material_values.hpp"
#include "material_parameter_overrides.hpp"
#include "transform_animation_runtime.hpp"
#include <algorithm>
namespace forge {
void TransformAnimationPlayback::synchronize_materials() {
    const auto resources = context_.services().available(Capability::Resources)
                               ? context_.services().resources()
                               : std::shared_ptr<RuntimeResourceService>{};
    material_inputs_.begin(resources);
    for (auto& [id, state] : states_) {
        if (!state.clip || state.clip->material_bindings.empty()) {
            state.material.reset();
            state.material_pending = false;
            if (state.material_error && !load_errors_.contains(id))
                errors_.erase(id);
            state.material_error = false;
            continue;
        }
        state.material_pending = true;
        try {
            const auto entity = context_.world().entity(id);
            if (!entity.has<MeshRenderer>())
                throw std::runtime_error("Material animation requires a MeshRenderer component");
            const auto& renderer = entity.get<MeshRenderer>();
            detail::validate_material_parameter_overrides(renderer.material_parameters);
            std::vector<MaterialParameterOverride> unanimated;
            for (const auto& value : renderer.material_parameters) {
                const auto at =
                    std::find_if(state.clip->material_bindings.begin(),
                                 state.clip->material_bindings.end(), [&](const auto& binding) {
                                     return binding.slot == value.slot && binding.key == value.key;
                                 });
                if (at == state.clip->material_bindings.end())
                    unanimated.push_back(value);
                else if (at->type != value.parameter.type)
                    throw std::runtime_error("Animated material override semantic type differs: " +
                                             value.key);
            }
            auto draw = material_inputs_.select(renderer);
            const auto requested = state.ticket.inspect();
            if (requested.identity != state.clip.identity() &&
                !resource_detail::terminal(requested.state))
                throw AnimationMaterialPending("Waiting for selected animated material clip");
            auto identity = asset_detail::prepared_model_draw_key(draw, false);
            if (!state.material || state.material->identity != identity ||
                state.material->clip != state.clip.identity() ||
                !detail::same_material_parameter_values(state.material->unanimated, unanimated)) {
                validate_animation_material_target(state.clip.get(), draw,
                                                   renderer.material_parameters);
                state.material = MaterialTarget{std::move(draw), std::move(identity),
                                                state.clip.identity(), std::move(unanimated)};
            }
            state.material_pending = false;
            if (state.material_error && !load_errors_.contains(id))
                errors_.erase(id);
            state.material_error = false;
        } catch (const AnimationMaterialPending&) {
            pending_ = true;
            if (state.material_error && !load_errors_.contains(id))
                errors_.erase(id);
            state.material_error = false;
        } catch (const std::exception& error_value) {
            state.material_error = true;
            error(id, error_value.what());
        }
    }
    try {
        material_inputs_.finish();
    } catch (const std::exception& error_value) {
        // No candidate can write before this consumer has admitted its complete
        // bounded demand into the existing shared resource owner.
        for (auto& [id, state] : states_)
            if (state.clip && !state.clip->material_bindings.empty()) {
                state.material_error = true;
                error(id, error_value.what());
            }
    }
}
} // namespace forge
