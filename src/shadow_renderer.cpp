#include "shadow_renderer.hpp"
#include <cmath>
namespace forge {
namespace {
static_assert(shadow_light_limit == 8 && shadow_cascade_limit == 8 &&
                  ShadowLighting::rows_per_light == 42,
              "Keep the private ForgeShadows.fxh packet layout in sync");
float gpu(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::runtime_error("Shadow receiver matrix exceeds finite GPU representation");
    return float(value);
}
void pack(ShadowLighting& output, unsigned slot, const CameraView& main_camera,
          const LightView& light, std::span<const ShadowView> views, unsigned resolution) {
    auto* rows = output.values.data() + slot * ShadowLighting::rows_per_light;
    for (unsigned i = 0; i < views.size(); ++i) {
        const auto& camera = views[i].camera;
        double relative_view[4][4]{};
        relative_view[3][3] = 1;
        for (unsigned r = 0; r < 3; ++r) {
            const auto& axis = r == 0 ? camera.right : r == 1 ? camera.up : camera.forward;
            for (unsigned c = 0; c < 3; ++c) {
                relative_view[r][c] = axis[c];
                relative_view[r][3] += axis[c] * (main_camera.position[c] - camera.position[c]);
            }
        }
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                double value = 0;
                for (unsigned k = 0; k < 4; ++k)
                    value += camera.projection[r * 4 + k] * relative_view[k][c];
                rows[i * 4 + r][c] = gpu(value);
            }
        rows[32 + i] = {gpu(views[i].begin), gpu(views[i].end), 0, 0};
    }
    rows[40] = {float(views.size()), light.shadow_bias, light.shadow_normal_bias,
                float(light.kind)};
    for (unsigned c = 0; c < 3; ++c)
        rows[41][c] = gpu(light.position[c] - main_camera.position[c]);
    rows[41][3] = float(resolution);
}
} // namespace
int ShadowRenderer::selection(EntityId entity) const {
    const auto found = selections_.find(entity);
    return found == selections_.end() ? -1 : found->second;
}
void ShadowRenderer::render(Diligent::IDeviceContext* context, const RenderScene& scene,
                            const CameraView& camera, std::uint32_t layers,
                            std::span<const ShadowCasterBounds> all_casters, const Draw& draw) {
    using namespace Diligent;
    lighting_ = {};
    selections_.clear();
    diagnostics_.clear();
    if (!scene.settings.shadows.enabled) {
        for (auto& map : maps_)
            map = {};
        return;
    }
    const auto& settings = scene.settings.shadows;
    const auto maximum = presentation_.device()->GetAdapterInfo().Texture.MaxTexture2DDimension;
    constexpr std::uint64_t budget = 256ull * 1024 * 1024;
    std::uint64_t selected_bytes = 0;
    unsigned slot = 0;
    for (const auto& source : scene.lights) {
        const auto& light = source.light;
        if (!light.cast_shadows || !(light.layers & layers))
            continue;
        try {
            if (settings.max_lights < 1 || settings.max_lights > shadow_light_limit ||
                settings.cascades < 1 || settings.cascades > shadow_cascade_limit ||
                !std::isfinite(settings.distance) || settings.distance <= 0)
                throw std::runtime_error("Invalid copied shadow settings");
            if (light.kind > 2 || !std::isfinite(light.shadow_bias) || light.shadow_bias < 0 ||
                !std::isfinite(light.shadow_normal_bias) || light.shadow_normal_bias < 0)
                throw std::runtime_error("Invalid copied shadow light values");
            if (slot >= settings.max_lights)
                throw std::runtime_error("Shadow light count exceeds the selected camera profile");
            const unsigned count = light.kind == 0 ? settings.cascades : light.kind == 1 ? 6 : 1;
            if (settings.resolution < 5 || settings.resolution > maximum ||
                count > shadow_cascade_limit)
                throw std::runtime_error("Shadow dimensions exceed the native device profile");
            const std::uint64_t bytes =
                std::uint64_t(settings.resolution) * settings.resolution * count * 4;
            if (bytes > budget - selected_bytes)
                throw std::runtime_error("Shadow depth payload exceeds the 256 MiB camera profile");
            auto& map = maps_[slot];
            std::unique_ptr<ShadowMapManager> candidate_map;
            auto* active = map.native.get();
            if (!map.native || map.resolution != settings.resolution || map.slices != count) {
                ShadowMapManager::InitInfo info;
                info.Format = TEX_FORMAT_D32_FLOAT;
                info.Resolution = settings.resolution;
                info.NumCascades = count;
                info.ShadowMode = SHADOW_MODE_PCF;
                candidate_map = std::make_unique<ShadowMapManager>();
                candidate_map->Initialize(presentation_.device(), presentation_.cache(), info);
                if (!candidate_map->GetSRV())
                    throw std::runtime_error("Native shadow map allocation failed");
                active = candidate_map.get();
            }
            std::vector<RenderBounds> casters;
            for (const auto& caster : all_casters)
                if (caster.layers & light.layers & layers)
                    casters.push_back(caster.bounds);
            const auto views = light.kind == 0 ? directional_shadow_views(*active, camera, light,
                                                                          settings, casters)
                                               : punctual_shadow_views(light, settings);
            if (views.empty())
                continue;
            ShadowLighting candidate;
            pack(candidate, slot, camera, light, views, settings.resolution);
            for (unsigned slice = 0; slice < views.size(); ++slice) {
                auto* depth = active->GetCascadeDSV(slice);
                context->SetRenderTargets(0, nullptr, depth,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->ClearDepthStencil(depth, CLEAR_DEPTH_FLAG, 1, 0,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float size = float(settings.resolution);
                Diligent::Viewport viewport{0, 0, size, size, 0, 1};
                context->SetViewports(1, &viewport, settings.resolution, settings.resolution);
                draw(views[slice].camera, light.layers & layers);
            }
            const auto offset = slot * ShadowLighting::rows_per_light;
            std::copy_n(candidate.values.begin() + offset, ShadowLighting::rows_per_light,
                        lighting_.values.begin() + offset);
            if (candidate_map)
                map = {settings.resolution, count, std::move(candidate_map)};
            lighting_.maps[slot] = map.native->GetSRV();
            selections_[source.entity] = int(slot);
            selected_bytes += bytes;
            ++slot;
        } catch (const std::exception& error) {
            if (diagnostics_.size() < 256) {
                Diagnostic diagnostic{
                    Severity::Error, "render.shadow.unavailable", error.what(), {}};
                diagnostic.context.asset = scene.scene;
                diagnostic.context.entity = source.entity;
                diagnostic.context.property = "forge.light.cast_shadows";
                diagnostic.context.source = "presentation";
                diagnostics_.push_back(std::move(diagnostic));
            }
        }
    }
    // Unselected resources must not keep old quality allocations alive indefinitely.
    for (unsigned unused = slot; unused < maps_.size(); ++unused)
        maps_[unused] = {};
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}
} // namespace forge
