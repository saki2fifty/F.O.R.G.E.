#pragma once
#include <forge/sdk_client.hpp>

// Call from an owner-thread fixed system before Physics. Velocity is XY metres
// per second; this queues a runtime command and never changes the saved Scene.
inline bool forge_gallery_launch_2d(const ForgeSdkWorldV1* host, const ForgeSdkEntityRefV1& actor,
                                    double x, double y) {
    const double velocity[] = {x, y};
    return forge::sdk::Client(host).set_linear_velocity_2d(actor.scene_uuid, actor.entity_uuid,
                                                           velocity);
}

// Read in PostPhysics. Events are copied, broadcast and expire at the next
// admitted synchronization. An End may refer to a retired realization: use its
// epoch/availability flags before treating either reference as a current body.
inline bool forge_gallery_sensor_event_2d(const ForgeSdkWorldV1* host, unsigned index,
                                          ForgeSdkPhysicsContact2DV1& event) {
    return forge::sdk::Client(host).contact_event_2d(index, event) == 1 && event.sensor;
}
