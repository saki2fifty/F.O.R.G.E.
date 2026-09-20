#pragma once
#include <forge/transform.hpp>
#include <nlohmann/json.hpp>
namespace forge::detail {
using Json = nlohmann::json;
Json migrate_transforms(const Json& source);
// Input has effective local components (from Flecs or detached intent projection).
LocalTransform read_local(const Json& components);
SpatialBinding read_binding(const Json& entity);
Json encode(LocalTranslation value);
Json encode(LocalRotation value);
Json encode(LocalScale value);
// Existing version gates protect extended numerical values from older editors.
// Positive-only documents keep their version. Never traverses opaque payloads.
void promote_scale_format(Json& document, const char* rows, unsigned required_version);
Json project_spatial(Json effective);
void validate_spatial(const Json& authored);
// Only writes specified channels. changed_only is for reparent compensation.
void write_local(Json& entity, const LocalTransform& current, const LocalTransform& desired,
                 TransformChannel channels, bool changed_only = false);
void write_world(Json& authored, const Json& effective, const std::string& id,
                 const AffineTransform& desired, TransformChannel channels,
                 bool changed_only = false);
void rebind(Json& authored, const Json& effective, const std::string& id, const Json& spatial,
            const std::string* structural_parent, ReparentMode mode);
// Inspector/legacy operation adapters: canonical storage remains local channels only.
void write_channel(Json& entity, const char* display_component, const Json& values);
void remap_spatial(Json& entity, AssetId source, AssetId destination,
                   const std::map<EntityId, EntityId>& remap);
} // namespace forge::detail
