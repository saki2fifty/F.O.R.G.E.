#pragma once
#include "asset_source_document.hpp"
#include <forge/material_graph.hpp>
#include <forge/shader_asset.hpp>
namespace forge {
struct MaterialGraphDocumentTraits {
    using Source = MaterialGraphSource;
    static constexpr std::size_t byte_limit = 1024 * 1024;
    static constexpr auto asset_type = ShaderAsset::type;
    static constexpr auto suffix = ".shader.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using MaterialGraphDocument = AssetSourceDocument<MaterialGraphDocumentTraits>;
} // namespace forge
