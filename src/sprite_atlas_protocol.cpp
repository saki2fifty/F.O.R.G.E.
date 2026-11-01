#include "sprite_atlas_source.hpp"
#include "sprite_frame_json.hpp"
#include "view_image_limits.h"
#include <forge/asset_build.hpp>
#include <stdexcept>
namespace forge::asset_detail {
std::string sprite_atlas_source_revision() {
    return asset_build_digest({{"sources_toolchain", FORGE_SPRITE_ATLAS_RECIPE_FINGERPRINT},
                               {"configuration", FORGE_SPRITE_ATLAS_RECIPE_CONFIGURATION}});
}
WorkerLimits sprite_atlas_source_limits() {
    WorkerLimits result;
    result.memory_bytes = 1024ull * 1024 * 1024;
    result.file_bytes = FORGE_VIEW_IMAGE_ENCODED_LIMIT;
    result.total_bytes = 256ull * 1024 * 1024;
    result.files = 520;
    result.seconds = 120;
    result.cpu_seconds = 110;
    result.cancellation_grace_ms = 250;
    return result;
}
std::string sprite_atlas_input_name(std::size_t index) {
    return "image-" + std::to_string(index) + ".texture";
}
nlohmann::json sprite_atlas_layout_document(const SpriteAtlasLayout& layout) {
    nlohmann::json frames = nlohmann::json::array();
    for (const auto& frame : layout.frames)
        frames.push_back(detail::sprite_frame_document(frame));
    return {{"format", "forge.sprite-atlas-layout"},
            {"version", 2},
            {"width", layout.width},
            {"height", layout.height},
            {"frames", std::move(frames)}};
}
} // namespace forge::asset_detail
