// Optional Gallery88 behavior. Compile with the matching SDK and TouchInput.hpp.
#include "TouchInput.hpp"
#include <cstring>
#include <flecs.h>
#include <forge/render_components.hpp>
#include <forge/transform_components.hpp>
void forge_register_system_TouchCursor(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.component<forge::SpriteRenderer>("forge.sprite_renderer");
    world.system<forge::LocalTranslation, forge::SpriteRenderer>("gameplay.GalleryTouchCursor")
        .kind(host->fixed_phase)
        .each([host](flecs::entity entity, forge::LocalTranslation& position,
                     forge::SpriteRenderer& image) {
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 reference{};
            if (client.entity_reference(entity, reference) != 1 ||
                std::strcmp(reference.scene_uuid, forge::examples::touch_scene) ||
                std::strcmp(reference.entity_uuid, forge::examples::touch_marker))
                return;
            forge::examples::TouchSample sample;
            if (!forge::examples::read_touch(client, sample))
                return;
            image.visible = sample.down;
            if (sample.down) {
                // The example camera authors a fixed16x10 canvas. Axis1 inputs
                // retain independent window coordinates; Axis2 normalizes length.
                position = {(sample.x - .5) * 16, (.5 - sample.y) * 10, 0};
                image.width = image.height = float(.4 + .8 * sample.pressure);
            }
        })
        .add(host->fixed_tag);
}
