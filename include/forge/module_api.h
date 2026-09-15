#ifndef FORGE_MODULE_API_H
#define FORGE_MODULE_API_H
#include <stdint.h>
#ifdef _WIN32
#define FORGE_EXPORT __declspec(dllexport)
#else
#define FORGE_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define FORGE_MODULE_API_VERSION 1u
/* All pointers are borrowed for the call. No exceptions or allocations cross the ABI.
   v1 is a stateless gameplay proof: only host-owned Position values survive reload.
   Deep plugins, arbitrary component registration and resource ownership are not v1 capabilities. */
typedef struct ForgeHostV1 {
    uint32_t size;
    uint32_t version;
    void* context;
    void (*translate)(void* context, float x, float y, float z);
} ForgeHostV1;
typedef struct ForgeModuleV1 {
    uint32_t size;
    uint32_t version;
    const char* module_id;
    const char* state_schema;
    void (*tick)(const ForgeHostV1* host, float seconds);
} ForgeModuleV1;
typedef const ForgeModuleV1* (*ForgeModuleEntry)(void);
FORGE_EXPORT const ForgeModuleV1* forge_module_v1(void);
#ifdef __cplusplus
}
#endif
#endif
