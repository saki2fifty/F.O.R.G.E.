#include "reflection_capture.hpp"
#include <algorithm>
#include <cstring>
#include <numbers>
namespace forge {
namespace {
constexpr std::array<Double3, 6> directions{
    {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<Double3, 6> ups{
    {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}}};
constexpr auto readiness_limit = std::chrono::seconds(60);
} // namespace
ReflectionCapture::ReflectionCapture(DiligentPresentation& presentation,
                                     std::shared_ptr<MeshResourceHost> resources, RenderScene scene,
                                     const ReflectionCaptureSettings& settings, EntityId camera)
    : presentation_(presentation), resources_(std::move(resources)), scene_(std::move(scene)) {
    validate_reflection_capture_settings(settings);
    if (!resources_ || !camera)
        throw std::invalid_argument("Reflection capture needs resources and camera identity");
    catalog_ = resources_->catalog();
    if (!catalog_)
        throw std::invalid_argument("Reflection capture needs a published project catalog");
    cube_.dimension = TextureDimension::Cube;
    cube_.format = TextureFormat::RGBA16Float;
    cube_.semantic = TextureSemantic::HdrColor;
    cube_.alpha = TextureAlpha::Opaque;
    cube_.width = cube_.height = settings.size;
    TextureLimits limits;
    limits.bytes = 64 * 1024 * 1024;
    validate_texture_metadata(cube_, limits);
    cube_.subresources.resize(6);
    for (unsigned i = 0; i < cameras_.size(); ++i) {
        auto& view = cameras_[i];
        view.entity = camera;
        auto& c = view.settings;
        c.vertical_fov = std::numbers::pi / 2;
        c.aspect = 1;
        c.near_plane = settings.near_plane;
        c.far_plane = settings.far_plane;
        c.layers = settings.layers;
        c.background_r = c.background_g = c.background_b = 0;
        c.background_a = 1;
        const auto& f = directions[i];
        const auto& u = ups[i];
        const Double3 r{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                        u[0] * f[1] - u[1] * f[0]};
        AffineTransform world;
        for (unsigned row = 0; row < 3; ++row) {
            world.m[row * 4] = r[row];
            world.m[row * 4 + 1] = u[row];
            world.m[row * 4 + 2] = f[row];
            world.m[row * 4 + 3] = settings.position[row];
        }
        view.view = camera_view(c, world, settings.size, settings.size);
    }
    // A lighting-map snapshot uses the source environment even when its authored
    // display sky is hidden. It contains no local/visible-depth recursive feedback.
    scene_.cameras.clear();
    scene_.reflection_probes.clear();
    scene_.settings.environment.sky = true;
    scene_.settings.screen_space_reflections.enabled = false;
    scene_.settings.ambient_occlusion.enabled = false;
    scene_.settings.temporal_anti_aliasing.enabled = false;
    scene_.settings.motion_blur.enabled = false;
    scene_.settings.depth_of_field.enabled = false;
    scene_.settings.bloom.enabled = false;
    scene_.settings.auto_exposure.enabled = false;
    scene_.settings.color_adjustments.enabled = false;
    renderer_ = std::make_unique<FrameRenderer>(presentation_, "Reflection capture");
    renderer_->resources(resources_);
    deadline_ = std::chrono::steady_clock::now() + readiness_limit;
}
ReflectionCapture::~ReflectionCapture() {
    if (owns_readback_)
        try {
            presentation_.cancel_readback();
        } catch (...) {
            // Shutdown still retires the native lease through presentation.
        }
}
void ReflectionCapture::stop(State terminal, std::string message) {
    terminal_ = terminal;
    diagnostic_ = std::move(message);
    if (owns_readback_) {
        presentation_.cancel_readback();
        state_ = State::Draining;
    } else {
        state_ = terminal;
        renderer_.reset();
        cube_.subresources.clear();
    }
}
void ReflectionCapture::cancel(std::string reason) {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Reflection cancellation requires its presentation thread");
    if (busy())
        stop(State::Cancelled, std::move(reason));
}
void ReflectionCapture::step(Diligent::IDeviceContext* context) {
    if (thread_ != std::this_thread::get_id() || !context)
        throw std::invalid_argument("Reflection capture requires its presentation context");
    if (!busy())
        return;
    try {
        if (state_ != State::Draining && resources_->catalog() != catalog_)
            stop(State::Cancelled, "Reflection capture cancelled because published assets changed");
        if (owns_readback_) {
            auto pixels = presentation_.take_hdr_readback(context);
            if (presentation_.readback_pending())
                return;
            owns_readback_ = false;
            if (state_ == State::Draining) {
                state_ = terminal_;
                renderer_.reset();
                cube_.subresources.clear();
                return;
            }
            if (!pixels || pixels->width != cube_.width || pixels->height != cube_.height ||
                pixels->rgba.size() != std::size_t(cube_.width) * cube_.height * 8)
                throw std::runtime_error("Reflection HDR readback differs from its capture extent");
            auto& face = cube_.subresources[face_];
            face.resize(pixels->rgba.size());
            std::memcpy(face.data(), pixels->rgba.data(), pixels->rgba.size());
            // A lighting map stores radiance, without surface opacity semantics.
            for (std::size_t at = 6; at < face.size(); at += 8) {
                face[at] = std::byte(0);
                face[at + 1] = std::byte(0x3c);
            }
            if (++face_ == cameras_.size()) {
                state_ = State::Ready;
                renderer_.reset();
                return;
            }
            deadline_ = std::chrono::steady_clock::now() + readiness_limit;
            state_ = State::Capturing;
        }
        if (state_ != State::Capturing)
            return;
        if (std::chrono::steady_clock::now() >= deadline_)
            throw std::runtime_error("Reflection capture resources did not become ready in time");
        auto* target = renderer_->render(context, scene_, std::span(&cameras_[face_], 1),
                                         cube_.width, cube_.height, 0, nullptr, 0, nullptr,
                                         FrameRenderer::Output::LinearHdr);
        if (renderer_->pending())
            return;
        if (!renderer_->diagnostics().empty() || renderer_->omitted_diagnostics())
            throw std::runtime_error(renderer_->diagnostics().empty()
                                         ? "Reflection capture has omitted rendering diagnostics"
                                         : renderer_->diagnostics().front().text);
        presentation_.readback_hdr(context, target);
        owns_readback_ = true;
        state_ = State::Readback;
    } catch (const std::exception& error) {
        stop(State::Failed, error.what());
    }
}
TextureData ReflectionCapture::take() {
    if (thread_ != std::this_thread::get_id() || state_ != State::Ready)
        throw std::logic_error(
            "Reflection source requires a completed capture on its owner thread");
    state_ = State::Taken;
    return std::move(cube_);
}
} // namespace forge
