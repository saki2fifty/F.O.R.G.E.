#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_components.hpp>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.component<forge::NavigationAgent>("forge.navigation_agent");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.system<const forge::NavigationAgent>()
        .kind(h->fixed_phase)
        .each([h](flecs::entity, const forge::NavigationAgent& a) {
            const auto id = a.navmesh.id.str();
            double start[3]{-8, .1, 0}, end[3]{8, .1, 0}, points[192]{};
            ForgeSdkNavResultV1 out{sizeof(out), 0, 0, 64, points};
            bool ok = (h->capabilities & FORGE_SDK_NAVIGATION) &&
                      h->navigation_query(h->context, id.c_str(), 1, start, end, &out) &&
                      out.status == 0 && out.count > 2;
            out.capacity = 1;
            ok = ok && h->navigation_query(h->context, id.c_str(), 1, start, end, &out) &&
                 out.status == 7 && out.count == 0;
            ok = ok && h->navigation_query(h->context, id.c_str(), 0, start, nullptr, &out) &&
                 out.status == 0 && out.count == 1;
            h->diagnostic(h->context, ok ? 1 : 3,
                          ok ? "navigation SDK queries passed" : "navigation SDK query failed");
        })
        .add(h->fixed_tag);
    return 1;
}
const char* deps[]{"forge.navigation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(api),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.navigation_probe",
                              "1",
                              deps,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_NAVIGATION,
                              FORGE_SDK_NAVIGATION | FORGE_SDK_DIAGNOSTICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
