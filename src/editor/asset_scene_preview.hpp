#pragma once
#include "../asset_preview_scene.hpp"
#include "camera.hpp"
#include "frame_renderer.hpp"
#include <forge/engine_assets.hpp>
#include <future>
namespace forge {
// Read-only asset inspection. One cancellable preparation job per owner; input
// is a copied catalog selection. FrameRenderer and the shared project resource
// host own all visual realization. No authored Scene or gameplay module runs.
class AssetScenePreview {
  public:
    AssetScenePreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                      std::filesystem::path project, std::shared_ptr<MeshResourceHost> host)
        : presentation_(presentation), context_(context), project_(std::move(project)),
          host_(std::move(host)), frame_(presentation) {
        frame_.resources(host_);
        camera.target = {0, 0, 0};
        camera.yaw = -.5f;
        camera.pitch = -.25f;
    }
    ~AssetScenePreview() { stop_.request_stop(); }
    EditorCamera camera;
    float exposure = 0, light_intensity = 3;
    bool source_lights = false;
    std::array<float, 3> background{.025f, .03f, .04f};
    const std::filesystem::path& project() const { return project_; }
    void select(std::shared_ptr<const AssetCatalog> catalog, AssetId asset,
                std::optional<unsigned> scene = {}) {
        if (catalog != catalog_)
            redraw_ = true; // Material/texture dependencies can change independently.
        catalog_ = std::move(catalog);
        if (asset == asset_ && scene == requested_scene_ && catalog_ == inspected_catalog_)
            return;
        inspected_catalog_ = catalog_;
        if (asset != asset_) {
            prepared_.reset();
            output_.Release();
            wanted_key_.clear();
            camera.target = {0, 0, 0};
            camera.distance = 6;
        }
        asset_ = asset;
        requested_scene_ = scene;
        try {
            if (!catalog_)
                throw std::runtime_error("Asset preview needs a project catalog");
            const auto& record = catalog_->records().at(asset);
            if (record.type != "model" && record.type != "mesh" && record.type != "material")
                throw std::runtime_error("This asset has no 3D preview provider");
            const auto& owner =
                record.subasset ? catalog_->records().at(record.subasset->owner) : record;
            const auto& receipt = owner.metadata.at("forge.import");
            const auto key = asset.str() + ":" + (scene ? std::to_string(*scene) : "default") +
                             ":" + receipt.at("key").get<std::string>() + ":" +
                             receipt.at("generation").dump();
            if (key == wanted_key_)
                return;
            wanted_key_ = key;
            wanted_type_ = record.type;
            wanted_owner_ = owner.id;
            stop_.request_stop();
            queued_ = true;
            preparation_failed_ = false;
            error_.clear();
        } catch (const std::exception& e) {
            stop_.request_stop();
            queued_ = false;
            preparation_failed_ = true;
            error_ = e.what();
        }
    }
    void pump() {
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto candidate = job_.get();
                if (!stop_.stop_requested()) {
                    prepared_ = std::move(candidate);
                    auto_frame_ = redraw_ = true;
                    error_.clear();
                }
            } catch (const std::exception& e) {
                if (!stop_.stop_requested()) {
                    error_ = e.what();
                    preparation_failed_ = true;
                }
            }
        }
        if (!job_.valid() && queued_) {
            queued_ = false;
            stop_ = std::stop_source{};
            const auto catalog = catalog_;
            job_ = std::async(std::launch::async, [project = project_, catalog, asset = asset_,
                                                   type = wanted_type_, owner = wanted_owner_,
                                                   source = requested_scene_,
                                                   stop = stop_.get_token()] {
                if (stop.stop_requested())
                    throw std::runtime_error("Asset preview cancelled");
                asset_detail::ModelPreviewScene result;
                if (type == "material") {
                    result.scene.scene = AssetId::generate();
                    RenderMesh object;
                    object.entity = EntityId::generate();
                    object.renderer.mesh = engine_primitive(1);
                    object.renderer.materials = {{"surface", {asset}}};
                    object.renderer.cast_shadows = object.renderer.receive_shadows = false;
                    result.scene.meshes = {object};
                    result.scene.settings.shadows.enabled = false;
                } else {
                    const auto selected =
                        asset_detail::load_model_selection(project, *catalog, owner, stop);
                    result = asset_detail::prepare_model_preview(
                        selected, source,
                        type == "mesh" ? AssetRef<MeshAsset>{asset} : AssetRef<MeshAsset>{});
                }
                if (stop.stop_requested())
                    throw std::runtime_error("Asset preview cancelled");
                return result;
            });
        }
    }
    void cancel() {
        stop_.request_stop();
        queued_ = false;
        inspected_catalog_.reset();
        wanted_key_.clear();
    }
    void frame_view() { auto_frame_ = redraw_ = true; }
    void retry() {
        inspected_catalog_.reset();
        wanted_key_.clear();
        select(catalog_, asset_, requested_scene_);
    }
    void changed_view() {
        auto_frame_ = false;
        redraw_ = true;
    }
    const asset_detail::ModelPreviewScene* data() const {
        return prepared_ ? &*prepared_ : nullptr;
    }
    const std::string& error() const { return error_; }
    std::string loading_state() const {
        std::string state = "queued=" + std::to_string(queued_) +
                            "; metadata job=" + std::to_string(job_.valid()) +
                            "; metadata=" + std::to_string(prepared_.has_value()) +
                            "; render pending=" + std::to_string(frame_.pending()) +
                            "; rendered frames=" + std::to_string(frame_.frames);
        for (const auto& diagnostic : frame_.diagnostics())
            state += "; " + diagnostic.text;
        return state;
    }
    bool pending() const {
        return queued_ || job_.valid() || (prepared_ && !preparation_failed_ && frame_.pending());
    }
    std::uint64_t render_count() const { return frame_.frames; }
    Diligent::ITextureView* output() const {
        return output_ ? output_->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    }
    Diligent::ITextureView* render(unsigned width, unsigned height) {
        pump();
        if (!prepared_ || queued_ || job_.valid() || preparation_failed_)
            return output();
        if (!width || !height || width > 2048 || height > 2048) {
            error_ = "Asset preview output dimensions exceed the 2048-pixel profile";
            return output();
        }
        const auto controls = std::array<float, 11>{
            camera.target[0], camera.target[1], camera.target[2], camera.yaw,
            camera.pitch,     camera.distance,  exposure,         light_intensity,
            background[0],    background[1],    background[2]};
        if (controls != controls_ || source_lights != previous_source_lights_ || width != width_ ||
            height != height_)
            redraw_ = true;
        if (!redraw_ && !frame_.pending())
            return output();
        try {
            auto scene = prepared_->scene;
            if (!source_lights || scene.lights.empty()) {
                Light light;
                light.intensity = light_intensity;
                auto key = light_view(light, {});
                key.direction = {.4 / std::sqrt(.96), -.8 / std::sqrt(.96), .4 / std::sqrt(.96)};
                scene.lights = {{light_, key}};
            }
            Camera lens;
            lens.background_r = background[0];
            lens.background_g = background[1];
            lens.background_b = background[2];
            lens.far_plane = EditorCamera::far_plane;
            const auto view = [&] {
                const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
                           forward = camera.forward();
                AffineTransform world;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    world.m[axis * 4] = right[axis];
                    world.m[axis * 4 + 1] = up[axis];
                    world.m[axis * 4 + 2] = forward[axis];
                    world.m[axis * 4 + 3] = eye[axis];
                }
                return std::array{
                    PreparedCamera{camera_, lens, camera_view(lens, world, width, height)}};
            };
            auto* rendered = frame_.render(context_, scene, view(), width, height, exposure);
            if (frame_.pending()) {
                // A retryable resource/pose failure can still be pending. Surface
                // its diagnostic while retaining the last complete image.
                if (!frame_.diagnostics().empty())
                    error_ = frame_.diagnostics().front().text;
                return output();
            }
            if (!frame_.diagnostics().empty())
                throw std::runtime_error(frame_.diagnostics().front().text);
            if (auto_frame_) {
                if (const auto bounds = frame_.bounds()) {
                    EditorCamera::Vec center;
                    double radius = 0;
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        center[axis] =
                            float(bounds->minimum[axis] * .5 + bounds->maximum[axis] * .5);
                        const double half = (bounds->maximum[axis] - bounds->minimum[axis]) * .5;
                        radius += half * half;
                    }
                    if (!camera.frame_sphere(center, std::sqrt(radius), float(width) / height))
                        throw std::runtime_error(
                            "Asset bounds exceed the editor camera framing profile");
                    rendered = frame_.render(context_, scene, view(), width, height, exposure);
                }
                auto_frame_ = false;
            }
            auto candidate = output_;
            if (!candidate || candidate->GetDesc().Width != width ||
                candidate->GetDesc().Height != height) {
                auto desc = rendered->GetTexture()->GetDesc();
                desc.Name = "FORGE retained asset preview";
                desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
                candidate.Release();
                presentation_.device()->CreateTexture(desc, nullptr, &candidate);
                if (!candidate)
                    throw std::runtime_error("Retained asset preview allocation failed");
            }
            Diligent::CopyTextureAttribs copy;
            copy.pSrcTexture = rendered->GetTexture();
            copy.pDstTexture = candidate;
            copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode =
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            context_->CopyTexture(copy);
            output_ = std::move(candidate);
            width_ = width;
            height_ = height;
            controls_ = {camera.target[0], camera.target[1], camera.target[2], camera.yaw,
                         camera.pitch,     camera.distance,  exposure,         light_intensity,
                         background[0],    background[1],    background[2]};
            previous_source_lights_ = source_lights;
            redraw_ = false;
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
            controls_ = controls;
            previous_source_lights_ = source_lights;
            width_ = width;
            height_ = height;
            redraw_ = false;
        }
        return output();
    }

  private:
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    std::shared_ptr<MeshResourceHost> host_;
    FrameRenderer frame_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> output_;
    std::shared_ptr<const AssetCatalog> catalog_, inspected_catalog_;
    AssetId asset_, wanted_owner_;
    std::optional<unsigned> requested_scene_;
    std::string wanted_key_, wanted_type_, error_;
    std::stop_source stop_;
    std::future<asset_detail::ModelPreviewScene> job_; // Joined before captured owner state dies.
    std::optional<asset_detail::ModelPreviewScene> prepared_;
    EntityId camera_ = EntityId::generate(), light_ = EntityId::generate();
    std::array<float, 11> controls_{};
    unsigned width_ = 0, height_ = 0;
    bool queued_ = false, redraw_ = true, auto_frame_ = true, previous_source_lights_ = false;
    bool preparation_failed_ = false;
};
} // namespace forge
