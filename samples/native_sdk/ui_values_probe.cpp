#include <flecs.h>
#include <forge/identity.hpp>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/ui_values.hpp>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    const auto type = world.component<forge::UiValues>("forge.ui_values");
    world.component<forge::StableId>("forge.stable_id");
    const auto* info = ecs_get_type_info(world, type);
    if (!info || info->size != sizeof(forge::UiValues) ||
        info->alignment != alignof(forge::UiValues))
        return 0;
    const forge::UiValues original{
        {{"long_named_number_for_owned_string_lifetime", .25}, {"width", 80}}};
    original.validate();
    auto first = world.entity().add(flecs::Prefab).set(original);
    auto copy = world.entity().add(flecs::Prefab).set(first.get<forge::UiValues>());
    auto changed = copy.get<forge::UiValues>();
    changed.values[0].name = "independent";
    changed.values[0].value = .75;
    copy.set(changed);
    const bool independent = first.get<forge::UiValues>() == original &&
                             copy.get<forge::UiValues>().values[0].value == .75;
    copy.destruct();
    first.destruct();
    return independent ? 1 : 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<const forge::UiValues, const forge::StableId>()
        .kind(host->fixed_phase)
        .each([host](const forge::UiValues& values, const forge::StableId& id) {
            for (const auto& value : values.values)
                if (value.name == "width" &&
                    !host->ui_publish_number(host->context, id.value.c_str(), "cpp_width",
                                             value.value))
                    host->diagnostic(host->context, 3, "Public UI Values publication failed");
        })
        .add(host->fixed_tag);
    host->diagnostic(host->context, 1, "Public UI Values vector/string lifecycle passed");
    return 1;
}
const char* dependencies[]{"forge.ui"};
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.ui_values_probe",
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
