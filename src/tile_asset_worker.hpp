#pragma once
#include "import_process.hpp"
#include "tile_asset_types.hpp"

namespace forge::asset_detail {
WorkerLimits tile_worker_limits();
std::vector<ArtifactFile> execute_tile_recipe(const ImportProcessRequest&, std::stop_token = {});
} // namespace forge::asset_detail
