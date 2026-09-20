#pragma once
#include <algorithm>
#include <forge/asset_importer.hpp>
namespace forge::asset_detail {
// Shared local-cache admission ceiling; descriptors may request a smaller budget.
// This does not relax the cache's file or aggregate byte limits.
inline CacheLimits import_cache_limits(const AssetImporterDescriptor& descriptor) {
    CacheLimits limits;
    limits.files = std::min<std::size_t>(4096, descriptor.limits.output_files);
    limits.total_bytes =
        std::min<std::uint64_t>(limits.total_bytes, descriptor.limits.output_bytes);
    limits.file_bytes = std::min(limits.file_bytes, limits.total_bytes);
    return limits;
}
} // namespace forge::asset_detail
