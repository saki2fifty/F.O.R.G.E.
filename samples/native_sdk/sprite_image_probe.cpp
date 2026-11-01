#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/render_components.hpp>

namespace {
struct SpriteImageProbe {
    std::uint32_t ticks = 0;
};
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::SpriteRenderer>("forge.sprite_renderer");
        world.component<forge::TileMapRenderer>("forge.tile_map_renderer");
        const auto type = world.component<SpriteImageProbe>("project.SpriteImageProbe")
                              .member<std::uint32_t>("ticks");
        return host->authoring_type(host->context, type, "project.sprite_image_probe", 1,
                                    R"({"ticks":0})", "Rendering", error, capacity);
    } catch (const std::exception& value) {
        if (capacity)
            std::snprintf(error, capacity, "%s", value.what());
        return 0;
    }
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.system<SpriteImageProbe>("project.ChangeSpriteImageFill")
        .kind(host->fixed_phase)
        .write<forge::SpriteRenderer>()
        .write<forge::TileMapRenderer>()
        .each([](flecs::entity actor, SpriteImageProbe& probe) {
            ++probe.ticks;
            if (probe.ticks > 7 || !actor.has<forge::SpriteRenderer>())
                return;
            auto image = actor.get<forge::SpriteRenderer>();
            if (probe.ticks >= 6) {
                // Switch the complete raw layout atomically before selecting stable keys.
                image.columns = image.rows = 1;
                image.margin_x = image.margin_y = image.spacing_x = image.spacing_y = 0;
                image.region_enabled = false;
                image.region_x = image.region_y = image.region_width = image.region_height = 0;
                image.frame_regions.clear();
                image.texture_frames = true;
                image.frame = probe.ticks == 6 ? UINT32_MAX : 7;
                actor.set(image);
                return;
            }
            const auto phase = probe.ticks % 3;
            if (actor.has<forge::TileMapRenderer>()) {
                auto tiles = actor.get<forge::TileMapRenderer>();
                tiles.mip_filter = phase;
                actor.set(tiles);
            }
            image.mip_filter = phase;
            image.image_mode = std::uint32_t(phase == 1   ? forge::SpriteImageMode::Sliced
                                             : phase == 2 ? forge::SpriteImageMode::Tiled
                                                          : forge::SpriteImageMode::Simple);
            image.pixels_per_meter = probe.ticks == 1 ? 4 : 6;
            image.border_left = image.border_right = image.border_top = image.border_bottom = 1;
            image.flip_x = probe.ticks >= 2;
            image.region_enabled = true;
            image.region_x = probe.ticks == 1 ? 1 : 0;
            image.region_y = probe.ticks == 1 ? 0 : 1;
            image.region_width = image.region_height = 3;
            if (probe.ticks == 2) {
                image.frame_regions = {{0, 0, 2, 2}, {2, 0, 1, 3}, {0, 2, 2, 1}};
                image.frame = 2;
            }
            if (probe.ticks == 3) {
                auto& frame = image.frame_regions.at(2);
                frame.rotated_clockwise = true;
                frame.source_width = frame.source_height = 3;
                frame.trim_x = frame.trim_y = 1;
            }
            if (probe.ticks == 4)
                for (auto& frame : image.frame_regions) {
                    // Sliced borders belong to every original canvas, including
                    // currently unused frames whose packed rectangles are smaller.
                    frame.source_width = frame.source_height = 3;
                }
            actor.set(image);
        })
        .add(host->fixed_tag);
    return 1;
}
const ForgeNativeSdkV1 module{sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.sprite_image_probe",
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
