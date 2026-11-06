#include "camera_render_textures.hpp"
#include "frame_renderer.hpp"

namespace forge {
using namespace Diligent;
CameraRenderTextures::CameraRenderTextures(DiligentPresentation& presentation,
                                           std::shared_ptr<MeshResourceHost> host)
    : presentation_(presentation), host_(std::move(host)),
      gpu_(presentation.device(), host_ ? host_->context_.RawPtr() : nullptr,
           render_texture_pixel_limit * 16, render_texture_output_limit * 2) {}
CameraRenderTextures::~CameraRenderTextures() = default;
void CameraRenderTextures::report(AssetId scene, const std::string& message) {
    if (diagnostics_.size() == 256)
        return;
    Diagnostic error{Severity::Error, "render.camera.output", message.substr(0, 4096), {}};
    error.context.asset = scene;
    error.context.property = "forge.camera.render_texture";
    error.context.source = "presentation";
    diagnostics_.push_back(std::move(error));
}
void CameraRenderTextures::invalidate_temporal_history() {
    inputs_.produced.clear();
    for (auto& [asset, target] : targets_) {
        (void)asset;
        if (target.renderer)
            target.renderer->invalidate_temporal_history();
    }
}
void CameraRenderTextures::submit() {
    gpu_.submit();
    gpu_.collect();
}
const RenderTextureInputs& CameraRenderTextures::render(
    IDeviceContext* context, const RenderScene& scene, MeshSceneRenderer& materials,
    std::uint32_t consumer_layers, double seconds, const TemporalFrameSample* sample,
    const RenderTextureOverlay* overlay, const RuntimeReflectionImages* reflection_images) {
    host_->check_thread();
    if (context != host_->context_.RawPtr())
        throw std::runtime_error("Camera outputs require the resource host's presentation context");
    diagnostics_.clear();
    pending_ = false;
    inputs_.produced.clear();
    gpu_.collect();
    try {
        if (scene_ != scene.scene) {
            for (auto& [asset, target] : targets_) {
                (void)asset;
                if (target.image)
                    gpu_.unload(target.image.source());
            }
            targets_.clear();
            inputs_.images.clear();
            scene_ = scene.scene;
        }
        const bool new_selection = epoch_ != host_->epoch_;
        epoch_ = host_->epoch_;
        host_->pump();
        std::set<AssetId> writers;
        std::size_t camera_count = 0;
        for (const auto& camera : scene.cameras)
            if (camera.camera.enabled && camera.camera.render_texture.id) {
                if (++camera_count > render_texture_camera_limit)
                    throw std::runtime_error("Render texture cameras exceed the 16-camera profile");
                writers.insert(camera.camera.render_texture.id);
                if (writers.size() > render_texture_output_limit)
                    throw std::runtime_error("Render textures exceed the four-output profile");
            }
        for (auto it = targets_.begin(); it != targets_.end();)
            if (!writers.contains(it->first)) {
                if (it->second.image)
                    gpu_.unload(it->second.image.source());
                it = targets_.erase(it);
            } else
                ++it;
        std::map<AssetId, RenderTextureData> definitions;
        for (const auto asset : writers) {
            auto& target = targets_[asset];
            if (!target.ticket || new_selection)
                target.ticket = host_->request_render_texture_values({asset});
            const auto info = target.ticket.inspect();
            if (info.state != ResourceState::Ready) {
                if (resource_detail::terminal(info.state))
                    throw std::runtime_error("Camera output configuration: " + info.diagnostic);
                pending_ = true;
                continue;
            }
            const auto admitted = host_->render_texture_values(target.ticket);
            if (!admitted) {
                pending_ = true;
                continue;
            }
            target.configuration = admitted;
            definitions.emplace(asset, admitted.get());
        }
        // Use actual immutable draw selections, including complete candidates;
        // do not resolve Material sources or catalog texture bindings again.
        materials.render_textures(inputs_);
        materials.update(scene);
        std::map<EntityId, std::set<AssetId>> reads;
        const auto main_reads = materials.render_texture_reads(scene, consumer_layers, true);
        for (const auto& camera : scene.cameras) {
            if (!camera.camera.enabled || !camera.camera.render_texture.id)
                continue;
            auto selected = materials.render_texture_reads(scene, camera.camera.layers, true);
            reads.emplace(camera.entity, std::move(selected));
        }
        if (pending_)
            return inputs_;
        const auto plan = prepare_render_texture_plan(scene, definitions, reads, main_reads);
        RenderTextureImages images;
        for (const auto& [asset, pass] : plan.passes) {
            (void)pass;
            auto& target = targets_.at(asset);
            const auto image = gpu_.acquire(target.configuration);
            if (target.image && target.image.source() != image.source())
                gpu_.unload(target.image.source());
            target.image = image;
            images.emplace(asset, image);
            if (!target.renderer) {
                target.renderer = std::make_unique<FrameRenderer>(presentation_, "Render texture");
                target.renderer->resources(host_);
            }
        }
        inputs_.images = std::move(images);
        for (const auto asset : plan.order) {
            const auto& pass = plan.passes.at(asset);
            if (!std::all_of(pass.reads.begin(), pass.reads.end(),
                             [&](AssetId read) { return inputs_.produced.contains(read); })) {
                pending_ = true;
                continue;
            }
            auto& target = targets_.at(asset);
            auto* rendered = target.renderer->render(
                context, scene, pass.composition.cameras, pass.definition.width,
                pass.definition.height, 0, nullptr, seconds, sample,
                FrameRenderer::Output::LinearHdrOpaque, &inputs_, nullptr, reflection_images);
            pending_ |= target.renderer->pending();
            if (target.renderer->pending() || !target.renderer->diagnostics().empty()) {
                for (const auto& error : target.renderer->diagnostics())
                    report(scene.scene, "Output " + asset.str() + ": " + error.text);
                continue;
            }
            if (overlay && *overlay)
                (*overlay)(context, asset, rendered, pass.definition.width, pass.definition.height);
            CopyTextureAttribs copy{rendered->GetTexture(),
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION, target.image.get(),
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
            context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            context->CopyTexture(copy);
            inputs_.produced.insert(asset);
        }
    } catch (const RenderTexturePending&) {
        inputs_.produced.clear();
        pending_ = true;
    } catch (const std::exception& e) {
        // Retained leases never make a failed frame's previous pixels current.
        inputs_.produced.clear();
        report(scene.scene, e.what());
    }
    return inputs_;
}
} // namespace forge
