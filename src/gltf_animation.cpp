#include "gltf_native.hpp"
#include "gltf_validation.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <tiny_gltf.h>

namespace forge::asset_detail {
NativeAnimationClip NativeGltfDocument::animation(std::size_t animation_index) const {
    using namespace gltf_detail;
    const auto& animations = array(source_.document, "animations", 100000);
    if (animation_index >= animations.size())
        throw std::runtime_error("glTF animation index is invalid");
    const auto& input = animations[animation_index];
    const auto& channels = array(input, "channels", 100000);
    const auto& samplers = array(input, "samplers", 100000);
    if (channels.empty() || samplers.empty())
        throw std::runtime_error("glTF animation requires channels and samplers");
    const auto& native = model();
    const auto& nodes = array(source_.document, "nodes", 100000);
    auto accessor_index = [&](const Json& value) {
        const auto result = size_value(value);
        if (result >= native.accessors.size())
            throw std::runtime_error("glTF animation accessor index is invalid");
        return result;
    };
    struct Decoded {
        std::shared_ptr<const std::vector<float>> values;
        unsigned components;
        std::size_t count;
    };
    std::map<std::size_t, Decoded> cache;
    std::size_t decoded_bytes = 0;
    auto decode = [&](std::size_t index) -> const Decoded& {
        if (const auto found = cache.find(index); found != cache.end())
            return found->second;
        const auto& accessor = native.accessors[index];
        const auto width = tinygltf::GetNumComponentsInType(accessor.type);
        constexpr std::size_t budget = 512 * 1024 * 1024;
        if (width <= 0 ||
            accessor.count > (budget - decoded_bytes) / sizeof(float) / unsigned(width))
            throw std::runtime_error("glTF animation decoded data exceeds 512 MiB");
        decoded_bytes += accessor.count * unsigned(width) * sizeof(float);
        auto values = floats(index);
        Decoded result{std::make_shared<const std::vector<float>>(std::move(values.values)),
                       values.components, values.count};
        return cache.emplace(index, std::move(result)).first->second;
    };
    struct Sampler {
        std::size_t output;
        NativeAnimationInterpolation interpolation;
        std::shared_ptr<const std::vector<float>> times;
    };
    std::vector<Sampler> admitted;
    std::set<std::size_t> checked_times;
    for (const auto& sampler : samplers) {
        const auto in = accessor_index(sampler.at("input"));
        const auto out = accessor_index(sampler.at("output"));
        const auto& a = native.accessors[in];
        if (a.type != TINYGLTF_TYPE_SCALAR || a.componentType != 5126 || a.normalized ||
            a.minValues.size() != 1 || a.maxValues.size() != 1)
            throw std::runtime_error("glTF animation input requires float SCALAR with bounds");
        if (a.bufferView >= 0) {
            const auto& view = native.bufferViews.at(a.bufferView);
            if (view.byteStride || view.target)
                throw std::runtime_error(
                    "glTF animation input cannot use vertex/index view layout");
        }
        const auto interpolation = sampler.value("interpolation", std::string("LINEAR"));
        const auto mode =
            interpolation == "LINEAR" ? NativeAnimationInterpolation::Linear
            : interpolation == "STEP" ? NativeAnimationInterpolation::Step
            : interpolation == "CUBICSPLINE"
                ? NativeAnimationInterpolation::CubicSpline
                : throw std::runtime_error("glTF animation interpolation is unsupported");
        const auto& times = decode(in).values;
        if (times->empty() ||
            (mode == NativeAnimationInterpolation::CubicSpline && times->size() < 2))
            throw std::runtime_error("glTF animation has insufficient input keys");
        if (checked_times.insert(in).second) {
            for (std::size_t i = 0; i < times->size(); ++i)
                if ((*times)[i] < 0 || (i && (*times)[i] <= (*times)[i - 1]))
                    throw std::runtime_error(
                        "glTF animation times must be nonnegative and strictly increasing");
            const auto close = [](double a, double b) {
                return std::abs(a - b) <= 1e-6 * std::max({1.0, std::abs(a), std::abs(b)});
            };
            if (!close(a.minValues[0], times->front()) || !close(a.maxValues[0], times->back()))
                throw std::runtime_error("glTF animation input bounds disagree with actual keys");
        }
        admitted.push_back({out, mode, times});
    }
    NativeAnimationClip result;
    std::set<std::pair<std::size_t, NativeAnimationPath>> targets;
    std::set<std::pair<std::size_t, unsigned>> checked_rotations;
    for (const auto& channel : channels) {
        const auto s = size_value(channel.at("sampler"));
        if (s >= admitted.size())
            throw std::runtime_error("glTF animation channel sampler index is invalid");
        const auto& target = channel.at("target");
        if (!target.is_object())
            throw std::runtime_error("glTF animation target must be an object");
        if (!target.contains("node")) {
            result.diagnostics.push_back("Core glTF channel without a node target was not bound");
            continue;
        }
        NativeAnimationTrack track;
        track.node = size_value(target.at("node"));
        if (track.node >= nodes.size())
            throw std::runtime_error("glTF animation target node index is invalid");
        const auto path = target.at("path").get<std::string>();
        track.path = path == "translation" ? NativeAnimationPath::Translation
                     : path == "rotation"  ? NativeAnimationPath::Rotation
                     : path == "scale"     ? NativeAnimationPath::Scale
                     : path == "weights"
                         ? NativeAnimationPath::Weights
                         : throw std::runtime_error("glTF animation target path is unsupported");
        if (!targets.emplace(track.node, track.path).second)
            throw std::runtime_error("glTF animation contains duplicate node/path targets");
        if (track.path != NativeAnimationPath::Weights && nodes[track.node].contains("matrix"))
            throw std::runtime_error("glTF animation cannot target matrix-authored node TRS");
        track.components = track.path == NativeAnimationPath::Rotation ? 4 : 3;
        if (track.path == NativeAnimationPath::Weights) {
            track.components =
                static_cast<unsigned>(hierarchy_.nodes[track.node].morph_weights.size());
            if (!track.components)
                throw std::runtime_error(
                    "glTF animation weights target requires mesh morph targets");
        }
        const auto& sample = admitted[s];
        const auto& accessor = native.accessors[sample.output];
        const auto expected_shape =
            track.path == NativeAnimationPath::Weights    ? TINYGLTF_TYPE_SCALAR
            : track.path == NativeAnimationPath::Rotation ? TINYGLTF_TYPE_VEC4
                                                          : TINYGLTF_TYPE_VEC3;
        const bool normalized_integer =
            accessor.normalized &&
            (accessor.componentType == 5120 || accessor.componentType == 5121 ||
             accessor.componentType == 5122 || accessor.componentType == 5123);
        if (accessor.type != expected_shape ||
            (accessor.componentType != 5126 && (!(track.path == NativeAnimationPath::Weights ||
                                                  track.path == NativeAnimationPath::Rotation) ||
                                                !normalized_integer)))
            throw std::runtime_error("glTF animation output format does not match target");
        if (accessor.bufferView >= 0) {
            const auto& view = native.bufferViews.at(accessor.bufferView);
            if (view.byteStride || view.target)
                throw std::runtime_error(
                    "glTF animation output cannot use vertex/index view layout");
        }
        track.interpolation = sample.interpolation;
        track.times = sample.times;
        const auto multiplier =
            sample.interpolation == NativeAnimationInterpolation::CubicSpline ? 3u : 1u;
        const auto count = track.times->size() * multiplier *
                           (track.path == NativeAnimationPath::Weights ? track.components : 1u);
        if (accessor.count != count)
            throw std::runtime_error("glTF animation output key count does not match input/target");
        track.values = decode(sample.output).values;
        if (track.path == NativeAnimationPath::Rotation &&
            checked_rotations.emplace(sample.output, multiplier).second)
            for (std::size_t key = 0; key < track.times->size(); ++key) {
                const auto offset = (key * multiplier + (multiplier == 3 ? 1 : 0)) * 4;
                const auto* q = track.values->data() + offset;
                const auto norm = std::hypot(std::hypot(double(q[0]), double(q[1])),
                                             std::hypot(double(q[2]), double(q[3])));
                if (std::abs(norm - 1) > 0.001)
                    throw std::runtime_error(
                        "glTF animation rotation key must be a unit quaternion");
            }
        result.duration = std::max(result.duration, double(track.times->back()));
        result.tracks.push_back(std::move(track));
    }
    return result;
}
} // namespace forge::asset_detail
