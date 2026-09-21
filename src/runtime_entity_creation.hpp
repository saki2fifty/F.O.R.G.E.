#pragma once
#include <cstdint>
#include <forge/identity.hpp>
#include <string>
namespace forge {
class WorldContext;
class Scene;
namespace detail {
// Intrinsic exact-SDK host operations. Scene membership remains authoritative;
// these are bounded process-local requests, not another entity hierarchy.
enum class RuntimeEntityState : unsigned { Pending = 1, Ready, Failed, Gone };
struct RuntimeEntityResult {
    RuntimeEntityState state = RuntimeEntityState::Pending;
    EntityRef reference;
    std::uint64_t native_entity = 0;
    std::string diagnostic;
};
void register_runtime_entity_creation(WorldContext&);
void attach_runtime_scene(Scene&);
std::uint64_t request_runtime_entity(WorldContext&, const std::string& module, AssetId scene,
                                     const std::string& name);
RuntimeEntityResult inspect_runtime_entity(WorldContext&, const std::string& module,
                                           std::uint64_t token);
bool release_runtime_entity(WorldContext&, const std::string& module, std::uint64_t token);
void publish_runtime_entities(WorldContext&);
} // namespace detail
} // namespace forge
