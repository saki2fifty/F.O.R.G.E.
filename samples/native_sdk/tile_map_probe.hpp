#pragma once
#include <forge/native_sdk.h>
// Matching-module test communication only; never authored or persisted.
struct TileMapProbe {
    const ForgeSdkWorldV1* host = nullptr;
    char scene[37]{}, entity[37]{}, error[256]{};
    uint64_t tile = 0, generation = 0, token = 0;
    uint32_t stage = 0;
};
