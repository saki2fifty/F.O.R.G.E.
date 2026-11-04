#include "fog_volume_view.hpp"
#include <algorithm>
#include <stdexcept>
namespace forge {
std::vector<PreparedFogVolume> prepare_fog_view(std::span<const RenderFogVolume> sources,
                                                Double3 origin, std::uint32_t camera_layers,
                                                const FogVolumeIssue& report) {
    if (sources.size() > 10000)
        throw std::invalid_argument("Fog Volume input exceeds the 10000-entity rendering profile");
    std::vector<PreparedFogVolume> result;
    if (!camera_layers)
        return result;
    result.reserve(sources.size());
    for (const auto& source : sources) {
        const auto& volume = source.volume;
        if (!volume.enabled || volume.density == 0 || !(volume.layers & camera_layers))
            continue;
        try {
            result.push_back({source.entity, volume.layers & camera_layers, volume.priority,
                              prepare_fog_volume(volume, source.world, origin)});
        } catch (const std::exception& error) {
            report(source.entity, error.what());
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.entity < b.entity;
    });
    return result;
}
FogVolumeSelection select_fog_volumes(std::span<const PreparedFogVolume> sources,
                                      std::uint32_t geometry_layers) {
    FogVolumeSelection result;
    if (!geometry_layers)
        return result;
    for (const auto& source : sources)
        if (source.layers & geometry_layers) {
            if (result.count < fog_volume_limit)
                result.values[result.count++] = source.sample;
            else
                ++result.overflow;
        }
    return result;
}
} // namespace forge
