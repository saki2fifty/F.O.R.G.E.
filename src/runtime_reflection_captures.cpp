#include "runtime_reflection_captures.hpp"
#include "frame_renderer.hpp"
#include "reflection_capture_view.hpp"
#include <limits>
#include <set>
namespace forge {
using namespace Diligent;
std::uint64_t RuntimeReflectionRealization::bytes(unsigned size) const {
    if (!presentation || size < 16 || size > 256 || (size & (size - 1)) != 0 ||
        size > presentation->device()->GetAdapterInfo().Texture.MaxTexture2DDimension)
        throw std::runtime_error("Runtime reflection face size exceeds the device profile");
    return std::uint64_t(size) * size * 6 * 8 + environment_output_bytes(16, size);
}
GpuEnvironment RuntimeReflectionRealization::upload(IRenderDevice* device, IDeviceContext* context,
                                                    unsigned size) const {
    (void)bytes(size);
    if (device != presentation->device())
        throw std::runtime_error("Runtime reflection device differs from its presentation owner");
    GpuEnvironment result;
    TextureDesc desc;
    desc.Name = "FORGE runtime reflection radiance";
    desc.Type = RESOURCE_DIM_TEX_CUBE;
    desc.Width = desc.Height = size;
    desc.ArraySize = 6;
    desc.MipLevels = 1;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &result.source);
    if (!result.source)
        throw std::runtime_error("Runtime reflection cube allocation failed");
    allocate_environment_outputs(*presentation, context, result, 16, size);
    return result;
}
RuntimeReflectionCaptures::RuntimeReflectionCaptures(DiligentPresentation& presentation,
                                                     std::shared_ptr<MeshResourceHost> host)
    : presentation_(presentation), host_(std::move(host)),
      gpu_(presentation.device(), host_ ? host_->context_.RawPtr() : nullptr, 64 * 1024 * 1024, 8,
           RuntimeReflectionRealization{&presentation}) {}
RuntimeReflectionCaptures::~RuntimeReflectionCaptures() = default;
void RuntimeReflectionCaptures::release(Target& target) {
    if (target.ready)
        gpu_.unload(target.ready.source());
    if (target.staging)
        gpu_.unload(target.staging.source());
    target = {};
}
void RuntimeReflectionCaptures::invalidate() {
    host_->check_thread();
    images_.clear();
    for (auto& [id, target] : targets_) {
        (void)id;
        release(target);
    }
    targets_.clear();
    time_.reset();
    if (renderer_)
        renderer_->invalidate_temporal_history();
    pending_ = false;
}
void RuntimeReflectionCaptures::submit() {
    gpu_.submit();
    gpu_.collect();
}
void RuntimeReflectionCaptures::report(AssetId scene, EntityId entity, const std::string& text) {
    if (diagnostics_.size() == 256)
        return;
    Diagnostic value{
        Severity::Warning, "render.reflection_probe.capture", text.substr(0, 4096), {}};
    value.context.asset = scene;
    value.context.entity = entity;
    value.context.property = "forge.reflection_probe.runtime_capture";
    value.context.source = "presentation";
    diagnostics_.push_back(std::move(value));
}
const RuntimeReflectionImages&
RuntimeReflectionCaptures::render(IDeviceContext* context, const RenderScene& scene,
                                  MeshSceneRenderer& materials, double seconds,
                                  const TemporalFrameSample* sample) {
    host_->check_thread();
    if (context != host_->context_.RawPtr())
        throw std::runtime_error("Runtime reflections require the resource host's context");
    diagnostics_.clear();
    pending_ = false;
    const auto now = sample ? sample->seconds : scene.simulation_seconds.value_or(seconds);
    const auto operation = sample ? sample->operation : AssetId{};
    if (scene_ != scene.scene || epoch_ != host_->epoch_ || operation_ != operation ||
        simulated_ != scene.simulation_seconds.has_value() || !std::isfinite(now) || now < 0 ||
        (time_ && now < *time_))
        invalidate();
    scene_ = scene.scene;
    epoch_ = host_->epoch_;
    operation_ = operation;
    simulated_ = scene.simulation_seconds.has_value();
    time_ = std::isfinite(now) && now >= 0 ? std::optional(now) : std::nullopt;
    images_.clear();
    gpu_.collect();
    std::map<EntityId, const RenderReflectionProbe*> selected;
    bool duplicate = false;
    std::size_t active = 0;
    for (const auto& probe : scene.reflection_probes)
        if (probe.probe.enabled && (probe.probe.texture.id || probe.probe.runtime_capture) &&
            (probe.probe.intensity != 0 || probe.probe.diffuse_intensity != 0)) {
            ++active;
            if (probe.probe.runtime_capture)
                duplicate |= !probe.entity || !selected.emplace(probe.entity, &probe).second;
        }
    if (duplicate || selected.size() > 2 || active > environment_probe_limit || !time_) {
        invalidate();
        report(scene.scene, {},
               "Runtime reflection profile requires at most two runtime probes, "
               "eight total probes, unique identities and finite nonnegative time; "
               "saved maps or global lighting retained.");
        return images_;
    }
    for (auto it = targets_.begin(); it != targets_.end();)
        if (!selected.contains(it->first)) {
            release(it->second);
            it = targets_.erase(it);
        } else
            ++it;
    std::optional<RenderScene> source;
    const RenderTextureInputs no_outputs;
    for (const auto& [id, probe] : selected) {
        auto& target = targets_[id];
        try {
            const auto volume = reflection_probe_volume(probe->probe, probe->world);
            const auto& value = probe->probe;
            const Configuration configuration{value.capture_size, volume.center, value.capture_near,
                                              value.capture_far, value.capture_layers};
            if (!target.generation || target.configuration != configuration) {
                release(target);
                if (generation_ == std::numeric_limits<std::uint64_t>::max())
                    throw std::runtime_error("Runtime reflection generation exhausted");
                target.generation = ++generation_;
                target.configuration = configuration;
                target.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            }
            const bool due = !target.completed || now - *target.completed >= value.capture_interval;
            const bool retry = !target.attempted ||
                               now - *target.attempted >= std::max(value.capture_interval, .25);
            if (due && (target.error.empty() || retry)) {
                target.attempted = now;
                if (!materials.render_texture_reads(scene, configuration.layers, true).empty())
                    throw std::runtime_error("Capture layers include a camera-output reader; "
                                             "exclude those surfaces with Capture layers");
                if (!renderer_) {
                    renderer_ =
                        std::make_unique<FrameRenderer>(presentation_, "Runtime reflection");
                    renderer_->resources(host_);
                }
                if (!target.staging) {
                    const unsigned slot = target.ready ? 1u - target.ready.source().slot : 0;
                    target.staging =
                        gpu_.acquire({id, target.generation, slot}, configuration.size);
                }
                ReflectionCaptureSettings settings;
                settings.size = configuration.size;
                settings.position = configuration.center;
                settings.near_plane = configuration.near_plane;
                settings.far_plane = configuration.far_plane;
                settings.layers = configuration.layers;
                const auto cameras = reflection_capture_cameras(settings, id);
                if (!source)
                    source = reflection_capture_scene(scene);
                bool complete = true;
                for (unsigned face = 0; face < cameras.size(); ++face) {
                    auto* rendered = renderer_->render(
                        context, *source, std::span(&cameras[face], 1), configuration.size,
                        configuration.size, 0, nullptr, seconds, sample,
                        FrameRenderer::Output::LinearHdrOpaque, &no_outputs);
                    if (renderer_->pending()) {
                        complete = false;
                        break;
                    }
                    if (!renderer_->diagnostics().empty() || renderer_->omitted_diagnostics())
                        throw std::runtime_error(renderer_->diagnostics().empty()
                                                     ? "Capture rendering omitted diagnostics"
                                                     : renderer_->diagnostics().front().text);
                    context->SetRenderTargets(0, nullptr, nullptr,
                                              RESOURCE_STATE_TRANSITION_MODE_NONE);
                    CopyTextureAttribs copy{
                        rendered->GetTexture(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                        target.staging.get().source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
                    copy.DstSlice = face;
                    context->CopyTexture(copy);
                }
                if (complete) {
                    convolve_environment(presentation_, context, target.staging.get(), 64, 64);
                    std::swap(target.ready, target.staging);
                    target.completed = now;
                    target.error.clear();
                    target.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
                } else if (std::chrono::steady_clock::now() >= target.deadline)
                    throw std::runtime_error(
                        "Capture resources did not become ready within 60 seconds");
                else
                    pending_ |= !target.ready;
            }
        } catch (const RenderTexturePending&) {
            if (std::chrono::steady_clock::now() >= target.deadline)
                target.error = "Capture material selection did not become ready within 60 seconds";
            else
                pending_ |= !target.ready;
        } catch (const std::exception& error) {
            target.error = error.what();
        }
        if (!target.error.empty())
            report(scene.scene, id,
                   target.error + (target.ready ? " (previous capture retained)"
                                                : " (saved map or global lighting retained)"));
        if (target.ready)
            images_.emplace(id, target.ready);
    }
    return images_;
}
} // namespace forge
