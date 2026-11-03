#include "render_texture_plan.hpp"
#include <algorithm>

namespace forge {
RenderTexturePlan
prepare_render_texture_plan(const RenderScene& scene,
                            const std::map<AssetId, RenderTextureData>& definitions,
                            const std::map<EntityId, std::set<AssetId>>& camera_reads,
                            const std::set<AssetId>& main_reads) {
    RenderTexturePlan result;
    std::size_t cameras = 0;
    std::uint64_t pixels = 0;
    for (const auto& camera : scene.cameras) {
        if (!camera.camera.enabled || !camera.camera.render_texture.id)
            continue;
        if (++cameras > render_texture_camera_limit)
            throw std::runtime_error("Render texture cameras exceed the 16-camera profile");
        const auto asset = camera.camera.render_texture.id;
        if (!result.passes.contains(asset)) {
            if (result.passes.size() == render_texture_output_limit)
                throw std::runtime_error("Render textures exceed the four-output profile");
            const auto definition = definitions.find(asset);
            if (definition == definitions.end())
                throw std::runtime_error("Render texture configuration is unavailable: " +
                                         asset.str());
            validate_render_texture(definition->second);
            const auto count = std::uint64_t(definition->second.width) * definition->second.height;
            if (count > render_texture_pixel_limit - pixels)
                throw std::runtime_error("Render textures exceed the 4194304-pixel profile");
            pixels += count;
            auto composition = prepare_game_cameras(scene, definition->second.width,
                                                    definition->second.height, {asset});
            if (!composition.diagnostics.empty() || composition.omitted_diagnostics)
                throw std::runtime_error("Render texture camera is invalid: " + asset.str());
            result.passes.emplace(
                asset, RenderTexturePass{definition->second, std::move(composition), {}});
        }
        if (const auto reads = camera_reads.find(camera.entity); reads != camera_reads.end())
            result.passes.at(asset).reads.insert(reads->second.begin(), reads->second.end());
    }
    const auto require_writer = [&](AssetId read) {
        if (!read || !result.passes.contains(read))
            throw std::runtime_error("Material render texture has no enabled camera writer: " +
                                     read.str());
    };
    for (auto read : main_reads)
        require_writer(read);
    for (const auto& [asset, pass] : result.passes) {
        (void)asset;
        for (auto read : pass.reads)
            require_writer(read);
    }
    // At most four outputs: deterministic admission in dependency order, without
    // persistent graph state, recursion or a second asset-dependency index.
    std::set<AssetId> completed;
    while (result.order.size() < result.passes.size()) {
        const auto ready =
            std::find_if(result.passes.begin(), result.passes.end(), [&](const auto& row) {
                return !completed.contains(row.first) &&
                       std::all_of(row.second.reads.begin(), row.second.reads.end(),
                                   [&](AssetId read) { return completed.contains(read); });
            });
        if (ready == result.passes.end())
            throw std::runtime_error(
                "Render texture feedback cycle; separate producer and consumer layers");
        result.order.push_back(ready->first);
        completed.insert(ready->first);
    }
    return result;
}
} // namespace forge
