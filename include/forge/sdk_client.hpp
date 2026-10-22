#pragma once
// Header-only convenience over the exact C boundary. Owns no host or world.
#include <chrono>
#include <forge/native_sdk.h>
namespace forge::sdk {
enum class Capability : uint32_t {
    Diagnostics = FORGE_SDK_DIAGNOSTICS,
    Profiling = FORGE_SDK_PROFILING,
    Rendering = FORGE_SDK_RENDERING,
    Physics = FORGE_SDK_PHYSICS,
    Audio = FORGE_SDK_AUDIO,
    Navigation = FORGE_SDK_NAVIGATION,
    Ui = FORGE_SDK_UI,
    Input = FORGE_SDK_INPUT,
    Resources = FORGE_SDK_RESOURCES,
    RuntimeEntities = FORGE_SDK_RUNTIME_ENTITIES,
    ControlInput = FORGE_SDK_CONTROL_INPUT,
    Game = FORGE_SDK_GAME,
    Animation = FORGE_SDK_ANIMATION,
    TileMaps = FORGE_SDK_TILE_MAPS,
    TileMapEdits = FORGE_SDK_TILE_MAP_EDITS
};
class Client {
  public:
    explicit Client(const ForgeSdkWorldV1* host) : host_(host) {}
    bool valid() const {
        return host_ && host_->size == sizeof(*host_) && host_->version == FORGE_NATIVE_SDK_ABI &&
               host_->query_capability;
    }
    ForgeSdkCapabilityV1 query(Capability cap,
                               uint32_t version = FORGE_SDK_CAPABILITY_VERSION) const {
        ForgeSdkCapabilityV1 result{sizeof(result), 0, 0, 0, 0};
        if (valid())
            host_->query_capability(host_->context, static_cast<uint32_t>(cap), version, &result);
        return result;
    }
    bool available(Capability cap) const { return query(cap).available != 0; }
    bool callable(Capability cap) const { return query(cap).callable != 0; }
    bool action(const char* id, ForgeSdkActionV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Input) && host_->read_action(host_->context, id, &out) == 1;
    }
    bool control(const char* id, ForgeSdkActionV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::ControlInput) &&
               host_->read_control(host_->context, id, &out) == 1;
    }
    // 1 hit, 0 miss, -1 invalid/unavailable. No stale output on miss/failure.
    int32_t raycast(const double origin[3], const double displacement[3],
                    ForgeSdkPhysicsHitV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics)
                   ? host_->raycast(host_->context, origin, displacement, &out)
                   : -1;
    }
    // Current-tick broadcast: 1 event, 0 end/unavailable, -1 invalid/removed target.
    int32_t animation_event(uint32_t index, ForgeSdkAnimationEventV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Animation) && host_->animation_read_event
                   ? host_->animation_read_event(host_->context, index, &out)
                   : 0;
    }
    int32_t entity_reference(uint64_t native_entity, ForgeSdkEntityRefV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::RuntimeEntities) && host_->entity_reference
                   ? host_->entity_reference(host_->context, native_entity, &out)
                   : 0;
    }
    int32_t animation_control(const char* scene, const char* entity, uint32_t operation) const {
        return callable(Capability::Animation) && host_->animation_control
                   ? host_->animation_control(host_->context, scene, entity, operation)
                   : 0;
    }
    int32_t animation_seek(const char* scene, const char* entity, double seconds) const {
        return callable(Capability::Animation) && host_->animation_seek
                   ? host_->animation_seek(host_->context, scene, entity, seconds)
                   : 0;
    }
    int32_t animation_playback(const char* scene, const char* entity,
                               ForgeSdkAnimationPlaybackV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Animation) && host_->animation_playback
                   ? host_->animation_playback(host_->context, scene, entity, &out)
                   : 0;
    }
    int32_t animation_root_motion(const char* scene, const char* entity,
                                  ForgeSdkAnimationRootMotionV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Animation) && host_->animation_root_motion
                   ? host_->animation_root_motion(host_->context, scene, entity, &out)
                   : 0;
    }
    bool character(const char* scene, const char* entity, ForgeSdkCharacterV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics) &&
               host_->character_state(host_->context, scene, entity, &out) == 1;
    }
    bool character_command(const char* scene, const char* entity, uint32_t operation,
                           const double* value = nullptr, const float* rotation = nullptr,
                           float speed = 0, bool flag = false) const {
        return callable(Capability::Physics) &&
               host_->character_command(host_->context, scene, entity, operation, value, rotation,
                                        speed, flag ? 1u : 0u) == 1;
    }
    int32_t raycast_filtered(const double origin[3], const double displacement[3], uint32_t mask,
                             bool sensors, ForgeSdkPhysicsHitV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics)
                   ? host_->raycast_filtered(host_->context, origin, displacement, mask,
                                             sensors ? 1u : 0u, &out)
                   : -1;
    }
    int32_t shape_cast(const ForgeSdkSweepV1& request, ForgeSdkPhysicsHitV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics) && host_->shape_cast
                   ? host_->shape_cast(host_->context, &request, &out)
                   : -1;
    }
    bool move_2d(const char* scene, const char* entity, const double position[2], double angle,
                 bool kinematic = false, bool clear_velocity = false) const {
        return callable(Capability::Physics) && host_->physics_move_2d &&
               host_->physics_move_2d(host_->context, scene, entity, position, angle,
                                      kinematic ? 1u : 0u, clear_velocity ? 1u : 0u) == 1;
    }
    bool apply_2d(const char* scene, const char* entity, ForgeSdkPhysicsLoad2DV1 load) const {
        load.size = sizeof(load);
        return callable(Capability::Physics) && host_->physics_apply_2d &&
               host_->physics_apply_2d(host_->context, scene, entity, &load) == 1;
    }
    bool set_linear_velocity_2d(const char* scene, const char* entity,
                                const double velocity[2]) const {
        return callable(Capability::Physics) && host_->physics_set_linear_velocity_2d &&
               host_->physics_set_linear_velocity_2d(host_->context, scene, entity, velocity) == 1;
    }
    bool set_angular_velocity_2d(const char* scene, const char* entity, double velocity) const {
        return callable(Capability::Physics) && host_->physics_set_angular_velocity_2d &&
               host_->physics_set_angular_velocity_2d(host_->context, scene, entity, velocity) == 1;
    }
    int32_t body_state_2d(const char* scene, const char* entity,
                          ForgeSdkPhysicsBodyState2DV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics) && host_->physics_body_state_2d
                   ? host_->physics_body_state_2d(host_->context, scene, entity, &out)
                   : -1;
    }
    int32_t point_velocity_2d(const char* scene, const char* entity, const double point[2],
                              double out[2]) const {
        if (!out)
            return -1;
        // Let the bridge copy the input before clearing an aliased output.
        if (callable(Capability::Physics) && host_->physics_point_velocity_2d)
            return host_->physics_point_velocity_2d(host_->context, scene, entity, point, out);
        out[0] = out[1] = 0;
        return -1;
    }
    int32_t raycast_2d(const double origin[2], const double displacement[2], uint32_t mask,
                       bool sensors, ForgeSdkPhysicsHit2DV1& out) const {
        if (callable(Capability::Physics) && host_->physics_raycast_2d) {
            out.size = sizeof(out);
            return host_->physics_raycast_2d(host_->context, origin, displacement, mask,
                                             sensors ? 1u : 0u, &out);
        }
        out = {};
        out.size = sizeof(out);
        return -1;
    }
    int32_t overlap_shape_2d(ForgeSdkOverlap2DV1 request, ForgeSdkEntityRefV1* out,
                             uint32_t capacity) const {
        if (!out || !capacity || capacity > FORGE_SDK_PHYSICS_OVERLAP_LIMIT)
            return -1;
        request.size = sizeof(request);
        for (uint32_t i = 0; i < capacity; ++i) {
            out[i] = {};
            out[i].size = sizeof(out[i]);
        }
        return callable(Capability::Physics) && host_->physics_overlap_shape_2d
                   ? host_->physics_overlap_shape_2d(host_->context, &request, out, capacity)
                   : -1;
    }
    int32_t shape_cast_2d(ForgeSdkSweep2DV1 request, ForgeSdkPhysicsHit2DV1& out) const {
        request.size = sizeof(request);
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics) && host_->physics_shape_cast_2d
                   ? host_->physics_shape_cast_2d(host_->context, &request, &out)
                   : -1;
    }
    int32_t contact_count_2d() const {
        return callable(Capability::Physics) && host_->physics_contact_count_2d
                   ? host_->physics_contact_count_2d(host_->context)
                   : -1;
    }
    int32_t contact_event_2d(uint32_t index, ForgeSdkPhysicsContact2DV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics) && host_->physics_contact_event_2d
                   ? host_->physics_contact_event_2d(host_->context, index, &out)
                   : -1;
    }
    bool navigation(const char* asset, uint32_t operation, const double start[3],
                    const double end[3], ForgeSdkNavResultV1& out) const {
        out.count = 0;
        out.status = FORGE_SDK_NAV_UNAVAILABLE;
        return callable(Capability::Navigation) &&
               host_->navigation_query(host_->context, asset, operation, start, end, &out) == 1;
    }
    int32_t navigation_agent(const char* scene, const char* entity,
                             ForgeSdkNavigationAgentV1& out) const {
        out = {};
        out.size = sizeof(out);
        out.version = 1;
        out.status = FORGE_SDK_NAV_UNAVAILABLE;
        return callable(Capability::Navigation) && host_->navigation_agent_state
                   ? host_->navigation_agent_state(host_->context, scene, entity, &out)
                   : 0;
    }
    bool audio(const char* scene, const char* entity, bool play) const {
        return audio_operation(scene, entity,
                               play ? FORGE_SDK_AUDIO_RESTART : FORGE_SDK_AUDIO_STOP);
    }
    bool audio_operation(const char* scene, const char* entity, uint32_t operation) const {
        return operation <= FORGE_SDK_AUDIO_ONE_SHOT && callable(Capability::Audio) &&
               host_->audio_source(host_->context, scene, entity, operation) == 1;
    }
    bool audio_one_shot(const char* scene, const char* entity) const {
        return audio_operation(scene, entity, FORGE_SDK_AUDIO_ONE_SHOT);
    }
    bool publish_number(const char* entity, const char* name, double value) const {
        return callable(Capability::Ui) &&
               host_->ui_publish_number(host_->context, entity, name, value) == 1;
    }
    bool allow_action(const char* name) const {
        return available(Capability::Ui) && host_->ui_allow_action(host_->context, name) == 1;
    }
    int32_t poll_action(const char* name, char* entity, uint32_t capacity) const {
        if (entity && capacity)
            entity[0] = 0;
        return callable(Capability::Ui)
                   ? host_->ui_poll_action(host_->context, name, entity, capacity)
                   : 0;
    }
    uint64_t request_resource(uint32_t kind, const char* asset,
                              uint32_t texture_variant = 0) const {
        return callable(Capability::Resources) && host_->resource_request
                   ? host_->resource_request(host_->context, kind, asset, texture_variant)
                   : 0;
    }
    bool inspect_resource(uint64_t token, ForgeSdkResourceV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Resources) && host_->resource_inspect &&
               host_->resource_inspect(host_->context, token, &out) == 1;
    }
    bool release_resource(uint64_t token) const {
        return callable(Capability::Resources) && host_->resource_release &&
               host_->resource_release(host_->context, token) == 1;
    }
    int32_t tile_map(const char* scene, const char* entity, ForgeSdkTileMapV1& out) const {
        out = {};
        out.size = sizeof(out);
        out.version = 1;
        return callable(Capability::TileMaps) && host_->tile_map_inspect
                   ? host_->tile_map_inspect(host_->context, scene, entity, &out)
                   : 0;
    }
    int32_t tile_layer(const char* scene, const char* entity, uint64_t generation, uint32_t index,
                       ForgeSdkTileLayerV1& out) const {
        out = {};
        out.size = sizeof(out);
        out.version = 1;
        return callable(Capability::TileMaps) && host_->tile_map_layer
                   ? host_->tile_map_layer(host_->context, scene, entity, generation, index, &out)
                   : 0;
    }
    int32_t tile_cells(const char* scene, const char* entity, uint64_t generation,
                       const ForgeSdkTileLocationV1* locations, ForgeSdkTileCellV1* out,
                       uint32_t count) const {
        if (callable(Capability::TileMaps) && host_->tile_map_cells)
            return host_->tile_map_cells(host_->context, scene, entity, generation, locations, out,
                                         count);
        if (out && count && count <= FORGE_SDK_TILE_BATCH_LIMIT)
            for (uint32_t i = 0; i < count; ++i)
                out[i] = {};
        return 0;
    }
    uint64_t write_tiles(const char* scene, const char* entity, uint64_t generation,
                         const ForgeSdkTileWriteV1* writes, uint32_t count) const {
        return callable(Capability::TileMapEdits) && host_->tile_map_write
                   ? host_->tile_map_write(host_->context, scene, entity, generation, writes, count)
                   : 0;
    }
    uint64_t reset_tiles(const char* scene, const char* entity, uint64_t generation) const {
        return callable(Capability::TileMapEdits) && host_->tile_map_reset
                   ? host_->tile_map_reset(host_->context, scene, entity, generation)
                   : 0;
    }
    int32_t tile_edit(uint64_t token, ForgeSdkTileEditV1& out) const {
        out = {};
        out.size = sizeof(out);
        out.version = 1;
        return callable(Capability::TileMaps) && host_->tile_edit_inspect
                   ? host_->tile_edit_inspect(host_->context, token, &out)
                   : 0;
    }
    bool release_tile_edit(uint64_t token) const {
        return callable(Capability::TileMaps) && host_->tile_edit_release &&
               host_->tile_edit_release(host_->context, token) == 1;
    }
    uint64_t request_entity(const char* name, const char* scene = nullptr) const {
        return callable(Capability::RuntimeEntities) && host_->entity_request
                   ? host_->entity_request(host_->context, scene, name)
                   : 0;
    }
    uint64_t request_prefab(const char* prefab, const char* name,
                            const char* scene = nullptr) const {
        return callable(Capability::RuntimeEntities) && host_->prefab_request
                   ? host_->prefab_request(host_->context, prefab, scene, name)
                   : 0;
    }
    bool inspect_entity(uint64_t token, ForgeSdkEntityV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::RuntimeEntities) && host_->entity_inspect &&
               host_->entity_inspect(host_->context, token, &out) == 1;
    }
    bool release_entity(uint64_t token) const {
        return callable(Capability::RuntimeEntities) && host_->entity_release &&
               host_->entity_release(host_->context, token) == 1;
    }
    bool authoring_type(uint64_t native_type, const char* key, uint32_t version,
                        const char* defaults_json, const char* category, char* error,
                        uint32_t capacity) const {
        return valid() && host_->authoring_type &&
               host_->authoring_type(host_->context, native_type, key, version, defaults_json,
                                     category, error, capacity) == 1;
    }
    bool diagnostic(uint32_t severity, const char* text) const {
        return callable(Capability::Diagnostics) &&
               host_->diagnostic(host_->context, severity, text) == 1;
    }
    bool profile(const char* name, double seconds) const {
        return callable(Capability::Profiling) &&
               host_->profile_sample(host_->context, name, seconds) == 1;
    }

  private:
    const ForgeSdkWorldV1* host_;
};
// Stack-local only: must end before the callback returns or world retires.
class ProfileScope {
  public:
    ProfileScope(Client client, const char* name)
        : client_(client), name_(name), start_(Clock::now()) {}
    ~ProfileScope() {
        client_.profile(name_, std::chrono::duration<double>(Clock::now() - start_).count());
    }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

  private:
    using Clock = std::chrono::steady_clock;
    Client client_;
    const char* name_;
    Clock::time_point start_;
};
} // namespace forge::sdk
