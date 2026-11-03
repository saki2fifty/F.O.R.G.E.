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
    const auto ui = world.component<forge::UiDocument>("forge.ui_document");
    const auto* ui_info = ecs_get_type_info(world, ui);
    return info && info->size == sizeof(forge::Camera) &&
           info->alignment == alignof(forge::Camera) && ui_info &&
           ui_info->size == sizeof(forge::UiDocument) &&
           ui_info->alignment == alignof(forge::UiDocument);
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
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
