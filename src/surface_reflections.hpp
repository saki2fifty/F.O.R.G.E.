#pragma once
#include "presentation_diligent.hpp"
#include "reflection_probe_volume.hpp"
#include "reflection_source_plan.hpp"
#include <forge/render_view.hpp>
#include <forge/scene_render_settings.hpp>
#include <span>
namespace forge {
// Current visible-depth operator. The existing view owns every input/result;
// material evaluation supplies real base response and replaceable specular light.
class SurfaceReflections {
  public:
    explicit SurfaceReflections(DiligentPresentation& presentation) : presentation_(presentation) {}
    SurfaceReflections(const SurfaceReflections&) = delete;
    SurfaceReflections& operator=(const SurfaceReflections&) = delete;
    struct SurfaceInput {
        Diligent::ITextureView* color = nullptr;
        Diligent::ITextureView* indirect = nullptr;
        Diligent::ITextureView* depth = nullptr;
        Diligent::ITextureView* surface = nullptr;
        Diligent::ITextureView* response = nullptr;
        Diligent::ITextureView* specular = nullptr;
        std::array<float, 16> projection{};
    };
    struct SourceLevel {
        Diligent::ITexture* color = nullptr;
        Diligent::ITexture* range = nullptr;
    };
    struct Input : SurfaceInput {
        SceneScreenSpaceReflections settings;
        std::span<const SourceLevel> sources;
    };
    struct Probe {
        ReflectionProbeVolume volume;
        Diligent::ITextureView* specular = nullptr;
        double intensity = 1, blend_distance = 1;
        bool box_projection = true;
        EntityId entity{};
    };
    // Borrowed, priority-ordered ready maps. Existing scene/resource owners retain
    // the leases; the pass neither loads assets nor chooses probe priority.
    struct LocalInput : SurfaceInput {
        CameraView camera;
        std::span<const Probe> probes;
    };
    void prepare(bool filtered = false);
    void prepare_sources();
    void build_sources(Diligent::IDeviceContext*, const SurfaceInput&,
                       std::span<const SourceLevel>);
    void apply(Diligent::IDeviceContext*, const Input&, Diligent::ITextureView* color_output,
               Diligent::ITextureView* indirect_output);
    void prepare_probes(bool specular_output);
    void apply_probes(Diligent::IDeviceContext*, const LocalInput&,
                      Diligent::ITextureView* color_output, Diligent::ITextureView* indirect_output,
                      Diligent::ITextureView* specular_output = nullptr);

  private:
    DiligentPresentation& presentation_;
    const std::thread::id thread_ = std::this_thread::get_id();
    struct PipelineState {
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> values;
    };
    std::array<PipelineState, 2> screens_, sources_, probes_;
    void validate_sources(const SurfaceInput&, std::span<const SourceLevel>) const;
};
} // namespace forge
