#pragma once
#include <forge/native_sdk.h>
// Data is read by the fixed system; each matched transform is owned by its entity.
struct ParallelMotion {
    double speed = 1;
};
int32_t forge_register_component_ParallelMotion(const ForgeSdkWorldV1*, char*, uint32_t);
void forge_register_system_ParallelMotion(const ForgeSdkWorldV1*);
