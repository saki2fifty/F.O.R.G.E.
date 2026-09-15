#include <forge/module_api.h>
static void tick(const ForgeHostV1* host, float seconds) {
    if (host && host->version == FORGE_MODULE_API_VERSION && host->size == sizeof(ForgeHostV1))
        host->translate(host->context, seconds, 0.0f, 0.0f);
}
FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void) {
    static const ForgeModuleV1 module = {sizeof(ForgeModuleV1), FORGE_MODULE_API_VERSION,
                                         "forge.sample.movement", "forge.position.v1", tick};
    return &module;
}
