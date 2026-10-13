#pragma once
#include "audio_selection.hpp"
#include <forge/asset_build.hpp>
#include <forge/audio_components.hpp>
#include <forge/project_paths.hpp>
#include <forge/resource.hpp>

namespace forge {
template <> struct ResourceTraits<AudioClipAsset> {
    using Data = asset_detail::AudioClipData;
};
inline std::string audio_clip_revision(const AssetRecord& record) {
    return asset_build_digest(nlohmann::json{{"id", record.id},
                                             {"type", record.type},
                                             {"source", path_utf8(record.source)},
                                             {"schema", record.schema_version},
                                             {"metadata", record.metadata}});
}
// The runtime catalog is immutable. Revision identity includes the loader's
// selected inputs, including legacy paths and cooked provenance.
inline ResourceTicket request_audio_clip(ResourcePool<AudioClipAsset>& pool,
                                         const std::filesystem::path& project,
                                         const AssetRecord& record) {
    return pool.request(AssetRef<AudioClipAsset>{record.id}, audio_clip_revision(record), 1,
                        [project, record](std::stop_token stop) {
                            if (stop.stop_requested())
                                throw std::runtime_error("Audio load cancelled");
                            auto value = std::make_unique<asset_detail::AudioClipData>(
                                asset_detail::load_audio_clip(project, record));
                            if (stop.stop_requested())
                                throw std::runtime_error("Audio load cancelled");
                            const auto bytes = value->pcm.capacity() * sizeof(float);
                            return ResourceCandidate<AudioClipAsset>{std::move(value), {bytes}};
                        });
}
} // namespace forge
