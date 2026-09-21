#pragma once
#include "authored_component.hpp"
#include "reconstructed_meta.hpp"
#include <memory>
namespace forge::detail {
// One owner-thread candidate generation. These native types contain engine hooks
// only. Retire all scene/template values before destroying the generation.
class AuthoredGeneration {
  public:
    AuthoredGeneration(flecs::world&, const nlohmann::json& copied);
    const std::vector<AuthoredCodec>& codecs() const { return codecs_; }
    // World finalization itself owns the last live generation's value/type order.
    void release_to_world() noexcept;

  private:
    std::vector<std::unique_ptr<ReconstructedMeta>> types_;
    std::vector<AuthoredCodec> codecs_;
};
} // namespace forge::detail
