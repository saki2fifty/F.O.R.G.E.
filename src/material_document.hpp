#pragma once
#include "asset_source_document.hpp"
#include <forge/material_source.hpp>
namespace forge {
struct MaterialDocumentTraits {
    using Source = MaterialSource;
    static constexpr auto byte_limit = material_source_byte_limit;
    static constexpr auto asset_type = MaterialAsset::type;
    static constexpr auto suffix = ".material.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using MaterialDocument = AssetSourceDocument<MaterialDocumentTraits>;
} // namespace forge
