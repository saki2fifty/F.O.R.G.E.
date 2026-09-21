#pragma once
#include "reflected_value.hpp"
namespace forge::detail {
// Private authoring-world reconstruction of a copied native Meta projection.
// The world and supplied engine reference adapters outlive this owner; all values
// of native_type() must retire before it. Never registers project callbacks.
class ReconstructedMeta {
  public:
    ReconstructedMeta(flecs::world, const nlohmann::json& projection,
                      std::span<const ReflectedAdapter> references = {});
    ~ReconstructedMeta();
    ReconstructedMeta(const ReconstructedMeta&) = delete;
    ReconstructedMeta& operator=(const ReconstructedMeta&) = delete;
    ecs_entity_t native_type() const { return type_; }

  private:
    ecs_world_t* world_ = nullptr;
    ecs_entity_t scope_ = 0, type_ = 0;
};
} // namespace forge::detail
