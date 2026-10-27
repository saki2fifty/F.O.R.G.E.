#include <box2d/box2d.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    for (int kind = 0; kind < 4; ++kind) {
        b2WorldDef config = b2DefaultWorldDef();
        b2WorldId world = b2CreateWorld(&config);
        if (kind != 0) {
            b2BodyDef body_config = b2DefaultBodyDef();
            body_config.type = kind == 3 ? b2_dynamicBody : b2_staticBody;
            b2BodyId body = b2CreateBody(world, &body_config);
            b2ShapeDef shape_config = b2DefaultShapeDef();
            shape_config.isSensor = kind == 2;
            b2Polygon box = b2MakeBox(1, 1);
            (void)b2CreatePolygonShape(body, &shape_config, &box);
        }
        b2World_Step(world, 1.f / 60, 4);
        int size = b2World_GetSnapshot(world, NULL, 0);
        unsigned char* bytes = malloc((size_t)size);
        if (!bytes || b2World_GetSnapshot(world, bytes, size) != size)
            return 2;
        if (!b2World_RestoreFromSnapshot(world, bytes, size))
            return 3;
        b2WorldId copy = b2CreateWorldFromSnapshot(bytes, size, 1);
        if (!b2World_IsValid(copy))
            return 4;
        b2Counters counters = b2World_GetCounters(copy);
        if (counters.bodyCount != (kind != 0) || counters.shapeCount != (kind != 0))
            return 5;
        b2DestroyWorld(copy);
        b2DestroyWorld(world);
        free(bytes);
    }
    puts("Native empty/static/sensor/dynamic snapshot reconstruction passed");
    return 0;
}
