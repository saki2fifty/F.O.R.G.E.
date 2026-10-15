#include <ObjectMaterialValues.hpp>
#include <cstdio>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
namespace {
struct MaterialValueProbe {
    std::uint32_t ticks = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::MeshRenderer>("forge.mesh_renderer");
        const auto type = world.component<MaterialValueProbe>("project.MaterialValueProbe")
                              .member<std::uint32_t>("ticks");
        return host->authoring_type(host->context, type, "project.material_value_probe", 1,
                                    R"({"ticks":0})", "Rendering", error, capacity);
    } catch (const std::exception& error_value) {
        if (capacity)
            std::snprintf(error, capacity, "%s", error_value.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<MaterialValueProbe>("project.ChangeObjectMaterialValues")
        .kind(host->fixed_phase)
        .each([](flecs::entity object, MaterialValueProbe& probe) {
            ++probe.ticks;
            bool ok = true;
            if (probe.ticks == 5)
                ok = forge_set_object_material_value(object, "surface", "roughnessFactor",
                                                     {forge::MaterialParameterType::Scalar, {.2f}});
            if (probe.ticks == 10)
                ok = forge_set_object_material_value(
                    object, "surface", "baseColorFactor",
                    {forge::MaterialParameterType::LinearColor4, {.03f, .8f, .1f, 1}});
            if (probe.ticks == 15)
                ok = forge_revert_object_material_value(object, "surface", "roughnessFactor");
            if (probe.ticks == 12) {
                auto renderer = object.get<forge::MeshRenderer>();
                std::reverse(renderer.material_parameters.begin(),
                             renderer.material_parameters.end());
                object.set(renderer);
            }
            if (!ok)
                throw std::runtime_error("Ordinary C++ object material value helper rejected");
        })
        .add(host->fixed_tag);
    return 1;
}
const ForgeNativeSdkV1 module{sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.material_value_probe",
                              "1",
                              nullptr,
                              0,
                              FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                              FORGE_SDK_RUNTIME,
                              0,
                              0,
                              &ecs_init,
                              &ecs_os_api,
                              schema,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &module;
}
