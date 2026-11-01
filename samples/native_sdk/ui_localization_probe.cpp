#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/ui_components.hpp>
#include <memory>

namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    const auto type = world.component<forge::UiLocalization>("forge.ui_localization");
    const auto* info = ecs_get_type_info(world.c_ptr(), type.id());
    if (!info || info->size != sizeof(forge::UiLocalization) ||
        info->alignment != alignof(forge::UiLocalization))
        return 0;
    const forge::UiLocalization value{{}, "en-Latn-GB-variant1"};
    auto first = world.entity().add(flecs::Prefab).set(value);
    auto copy = world.entity().add(flecs::Prefab).set(first.get<forge::UiLocalization>());
    auto changed = copy.get<forge::UiLocalization>();
    changed.locale = "fr";
    copy.set(changed);
    const bool independent = first.get<forge::UiLocalization>().locale == value.locale &&
                             copy.get<forge::UiLocalization>().locale == "fr";
    copy.destruct();
    first.destruct();
    return independent ? 1 : 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    auto switched = std::make_shared<bool>(false);
    world.system<forge::UiLocalization>()
        .kind(host->post_physics_phase)
        .each([host, switched](flecs::entity entity, forge::UiLocalization& value) {
            if (*switched || !value.catalog.id)
                return;
            value.locale = "fr";
            entity.modified<forge::UiLocalization>();
            *switched = true;
            host->diagnostic(host->context, 1,
                             "Public UI Localization value/lifetime/write passed");
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[]{"forge.ui"};
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.ui_localization_probe",
                           "1",
                           dependencies,
                           1,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_UI,
                           FORGE_SDK_UI | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
