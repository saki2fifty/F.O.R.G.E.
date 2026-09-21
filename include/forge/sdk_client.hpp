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
    Resources = FORGE_SDK_RESOURCES
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
    // 1 hit, 0 miss, -1 invalid/unavailable. No stale output on miss/failure.
    int32_t raycast(const double origin[3], const double displacement[3],
                    ForgeSdkPhysicsHitV1& out) const {
        out = {};
        out.size = sizeof(out);
        return callable(Capability::Physics)
                   ? host_->raycast(host_->context, origin, displacement, &out)
                   : -1;
    }
    bool navigation(const char* asset, uint32_t operation, const double start[3],
                    const double end[3], ForgeSdkNavResultV1& out) const {
        out.count = 0;
        out.status = FORGE_SDK_NAV_UNAVAILABLE;
        return callable(Capability::Navigation) &&
               host_->navigation_query(host_->context, asset, operation, start, end, &out) == 1;
    }
    bool audio(const char* scene, const char* entity, bool play) const {
        return callable(Capability::Audio) &&
               host_->audio_source(host_->context, scene, entity, play) == 1;
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
