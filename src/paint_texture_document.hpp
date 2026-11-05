#pragma once
#include "asset_source_document.hpp"
#include "paint_texture_source.hpp"
namespace forge {
struct PaintTextureDocumentTraits {
    using Source = PaintTextureSource;
    static constexpr auto byte_limit = paint_texture_source_byte_limit;
    static constexpr auto asset_type = TextureAsset::type;
    static constexpr auto suffix = ".paint-texture.json";
    static Source create(AssetId id) { return Source::create(id); }
    static std::size_t history_weight(const Source& before, const Source& after) {
        return PaintTextureValue::history_weight(before.document, after.document);
    }
    static void validate_edit(const Source& before, const Source& after) {
        if (before.document.width() != after.document.width() ||
            before.document.height() != after.document.height() ||
            before.document.semantic() != after.document.semantic())
            throw std::runtime_error(
                "Paint texture edits must retain canvas size and interpretation");
    }
};
using PaintTextureDocument = AssetSourceDocument<PaintTextureDocumentTraits>;
} // namespace forge
