#pragma once
#include "fog_volume_geometry.hpp"
#include <forge/render_scene.hpp>
#include <functional>
#include <string_view>
#include <vector>
namespace forge {
// Detached, per-view preparation from the existing RenderScene. Source and
// native lifetime remain with Scene/frame owners; no registry is retained here.
struct PreparedFogVolume {
    EntityId entity;
    std::uint32_t layers;
    std::int32_t priority;
    FogVolumeSample sample;
};
struct FogVolumeSelection {
    std::array<FogVolumeSample, fog_volume_limit> values{};
    std::size_t count = 0, overflow = 0;
    std::span<const FogVolumeSample> samples() const { return {values.data(), count}; }
};
using FogVolumeIssue = std::function<void(EntityId, std::string_view)>;
// Reporter strings are borrowed for the synchronous call only. Invalid active
// volumes are reported individually; valid sources continue. Result order is
// priority descending, then durable entity identity ascending.
std::vector<PreparedFogVolume> prepare_fog_view(std::span<const RenderFogVolume>, Double3 origin,
                                                std::uint32_t camera_layers, const FogVolumeIssue&);
// Layers filter before the sixteen-source bound. Geometry-mask results can be
// reused for every compatible draw in this view.
FogVolumeSelection select_fog_volumes(std::span<const PreparedFogVolume>,
                                      std::uint32_t geometry_layers);
} // namespace forge
