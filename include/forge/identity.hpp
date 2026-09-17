#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
namespace forge {
namespace detail {
using UuidBytes = std::array<std::uint8_t, 16>;
UuidBytes uuid_v4();
UuidBytes parse_uuid(std::string_view text);
std::string uuid_text(const UuidBytes& bytes);
} // namespace detail
template <class Tag> class PersistentId {
  public:
    PersistentId() = default;
    static PersistentId generate() { return PersistentId(detail::uuid_v4()); }
    static PersistentId parse(std::string_view text) {
        return PersistentId(detail::parse_uuid(text));
    }
    std::string str() const { return detail::uuid_text(bytes_); }
    explicit operator bool() const { return bytes_ != detail::UuidBytes{}; }
    auto operator<=>(const PersistentId&) const = default;

  private:
    explicit PersistentId(detail::UuidBytes bytes) : bytes_(bytes) {}
    detail::UuidBytes bytes_{};
};
struct EntityIdTag;
struct AssetIdTag;
using EntityId = PersistentId<EntityIdTag>;
using AssetId = PersistentId<AssetIdTag>;
struct PersistentEntityId {
    EntityId value;
};
struct EntityRef {
    AssetId scene;
    EntityId entity;
    auto operator<=>(const EntityRef&) const = default;
};
// Only registered/known reference fields may call this; never scan arbitrary JSON.
inline EntityRef remap_entity_ref(EntityRef ref, AssetId source, AssetId destination,
                                  const std::map<EntityId, EntityId>& entities) {
    if (ref.scene != source)
        return ref;
    const auto target = entities.find(ref.entity);
    return target == entities.end() ? ref : EntityRef{destination, target->second};
}
template <class Tag> void to_json(nlohmann::json& j, const PersistentId<Tag>& id) {
    if (!id)
        throw std::runtime_error("Cannot serialize an empty persistent ID");
    j = id.str();
}
template <class Tag> void from_json(const nlohmann::json& j, PersistentId<Tag>& id) {
    id = PersistentId<Tag>::parse(j.get<std::string>());
}
inline void to_json(nlohmann::json& j, const EntityRef& ref) {
    j = {{"scene", ref.scene}, {"entity", ref.entity}};
}
inline void from_json(const nlohmann::json& j, EntityRef& ref) {
    ref = {j.at("scene").get<AssetId>(), j.at("entity").get<EntityId>()};
}
} // namespace forge
