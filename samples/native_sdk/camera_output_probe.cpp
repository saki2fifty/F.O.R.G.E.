#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>
#include <forge/ui_components.hpp>

namespace {
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    const auto type = world.component<forge::Camera>("forge.camera");
    const auto* info = ecs_get_type_info(world, type);
    const auto aim = world.component<forge::CameraAim>("forge.camera_aim");
    const auto* aim_info = ecs_get_type_info(world, aim);
    const auto follow = world.component<forge::CameraFollow>("forge.camera_follow");
    const auto* follow_info = ecs_get_type_info(world, follow);
    const auto ui = world.component<forge::UiDocument>("forge.ui_document");
    const auto* ui_info = ecs_get_type_info(world, ui);
    const auto surface = world.component<forge::UiSurfaceInput>("forge.ui_surface_input");
    const auto* surface_info = ecs_get_type_info(world, surface);
    return info && info->size == sizeof(forge::Camera) &&
           info->alignment == alignof(forge::Camera) && ui_info &&
           ui_info->size == sizeof(forge::UiDocument) &&
           ui_info->alignment == alignof(forge::UiDocument) && surface_info &&
           surface_info->size == sizeof(forge::UiSurfaceInput) &&
           surface_info->alignment == alignof(forge::UiSurfaceInput) && aim_info &&
           aim_info->size == sizeof(forge::CameraAim) &&
           aim_info->alignment == alignof(forge::CameraAim) && follow_info &&
           follow_info->size == sizeof(forge::CameraFollow) &&
           follow_info->alignment == alignof(forge::CameraFollow);
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<forge::CameraFollow>("project.AdjustCameraFollow")
        .kind(host->fixed_phase)
        .each([](forge::CameraFollow& follow) {
            follow.weight = .25;
            follow.offset_y = 1.5;
            follow.offset_space = std::uint32_t(forge::CameraFollowSpace::Target);
        })
        .add(host->fixed_tag);
    world.system<forge::CameraAim>("project.AdjustCameraAim")
        .kind(host->fixed_phase)
        .each([](forge::CameraAim& aim) {
            aim.weight = .5;
            aim.offset_y = .75;
        })
        .add(host->fixed_tag);
    world.system<forge::Camera>("project.ReturnCameraToMain")
        .kind(host->fixed_phase)
        .each([](forge::Camera& camera) {
            if (camera.render_texture.id) {
                camera.render_texture = {};
                camera.viewport_width = .5;
                ++camera.order;
            }
        })
        .add(host->fixed_tag);
    world.system<forge::UiDocument>("project.ReturnUiToMain")
        .kind(host->fixed_phase)
        .each([](forge::UiDocument& document) {
            if (document.render_texture.id) {
                document.render_texture = {};
                ++document.layer;
            }
        })
        .add(host->fixed_tag);
    world.system<forge::UiSurfaceInput>("project.UseMeshSurfaceInput")
        .kind(host->fixed_phase)
        .each([](forge::UiSurfaceInput& surface) {
            surface.geometry = std::uint32_t(forge::UiSurfaceGeometry::Mesh);
        })
        .add(host->fixed_tag);
    host->diagnostic(host->context, 1, "Public Camera output layout admitted");
    return 1;
}
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.camera_output_probe",
                           "1",
                           nullptr,
                           0,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           0,
                           FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schema,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
