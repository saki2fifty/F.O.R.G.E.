#pragma once
#include <nlohmann/json.hpp>
#include <vector>
namespace forge::asset_detail {
struct MorphWeightsSample {
    // Candidate-local model node, resolved by the model instance binding.
    std::size_t node = 0;
    std::vector<float> weights;
};
// Immutable companion curves for glTF weights, which the pinned Ozz converter
// does not evaluate. No clocks, mutable playback state or ECS objects are owned.
class MorphAnimation {
  public:
    explicit MorphAnimation(const nlohmann::json& tracks);
    std::vector<MorphWeightsSample> sample(double seconds) const;
    std::size_t resident_bytes() const;

  private:
    struct Track {
        std::size_t node = 0;
        unsigned components = 0, interpolation = 0;
        std::vector<float> times, values;
    };
    std::vector<Track> tracks_;
};
} // namespace forge::asset_detail
