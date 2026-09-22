#pragma once
#include <forge/asset_ref.hpp>
#include <forge/engine_texture_assets.hpp>
#include <forge/primitive_catalog.hpp>
#include <optional>
#include <span>
#include <vector>
namespace forge {
// Immutable engine-owned UUIDv4 allocations. Never derive an asset UUID from a
// process handle, source path or enum ordinal. This table is append-only.
struct EngineAsset {
    AssetId id;
    const char* type;
    const char* name;
    std::optional<unsigned> primitive;
};
inline std::span<const EngineAsset> engine_assets() {
    static const std::vector<EngineAsset> values = [] {
        std::vector<EngineAsset> result{
            {AssetId::parse("80c55df2-1abb-42b0-9591-502e783cdc97"), MeshAsset::type,
             primitive_names[0], 0},
            {AssetId::parse("1c32b0ce-a595-49dc-910b-79f2d696f726"), MeshAsset::type,
             primitive_names[1], 1},
            {AssetId::parse("0b54bb83-8277-4d76-9da3-95959afa6ab6"), MeshAsset::type,
             primitive_names[2], 2},
            {AssetId::parse("68178d81-bcbf-4948-b0a0-cc6a5dbcc224"), MeshAsset::type,
             primitive_names[3], 3},
            {AssetId::parse("aeee6aa2-7d00-4bf4-b1e3-b5f9a3f2ca76"), MeshAsset::type,
             primitive_names[5], 5},
            {AssetId::parse("2469ed8c-9b0d-4741-81f7-88f07f78ff41"), MeshAsset::type,
             primitive_names[6], 6},
            {AssetId::parse("7bc216f6-5a76-45f7-bc4d-0dd6d8828bb8"), MeshAsset::type,
             primitive_names[7], 7},
            {AssetId::parse("05420c39-1aa8-46ae-b73e-f61680bea5db"), MeshAsset::type,
             primitive_names[8], 8},
            {AssetId::parse("12965ccd-e94f-44b3-9524-ad820297266c"), MeshAsset::type,
             primitive_names[9], 9},
            {AssetId::parse("1a57217f-2976-406a-a199-0bd991f304ff"), MeshAsset::type,
             primitive_names[10], 10},
            {AssetId::parse("cc3cce9b-179b-4389-90de-702635eb7446"), MeshAsset::type,
             primitive_names[11], 11},
            {AssetId::parse("a7edbc0c-63f3-4486-ae86-749e99be95e7"), MeshAsset::type,
             primitive_names[12], 12},
            {AssetId::parse("9193559e-bbcf-4fa1-a849-1d922d47e3a6"), MeshAsset::type,
             primitive_names[13], 13},
            {AssetId::parse("c34dcc5b-659a-49b6-a44c-aa3f3cbdd868"), MeshAsset::type,
             primitive_names[14], 14},
            {AssetId::parse("3d44a5fb-c71a-4432-9ffa-cbc63bd571c3"), MeshAsset::type,
             primitive_names[15], 15},
            {AssetId::parse("152aab52-8daa-4c12-8989-ff1f6a26bb2f"), MeshAsset::type,
             primitive_names[16], 16},
            {AssetId::parse("e9bf3b7d-ce51-482a-ac45-5665aee4c553"), MeshAsset::type,
             primitive_names[17], 17},
            {AssetId::parse("2b1c4da0-69da-4b9b-80d7-0122e4334804"), MeshAsset::type,
             primitive_names[18], 18},
            {AssetId::parse("79ada7a5-c64e-4241-8cf9-21a0e65b1007"), MeshAsset::type,
             primitive_names[19], 19},
            {AssetId::parse("428602c7-ed4b-4543-bb12-0c07f2556131"), MeshAsset::type,
             primitive_names[20], 20},
            {AssetId::parse("9d087798-a173-48e0-938d-7607eed42f91"),
             MaterialAsset::type,
             "Default surface",
             {}},
            {AssetId::parse("688cb877-0073-48bc-8fd9-141a4e28e210"),
             MaterialAsset::type,
             "Two-sided surface",
             {}},
            {AssetId::parse("39bab1fe-073f-4b6f-abd2-6ee2fc780696"),
             MaterialAsset::type,
             "Legacy blockout",
             {}},
            {AssetId::parse("d60dd541-31eb-4fbf-b720-63e8ffa1702a"),
             MaterialAsset::type,
             "Error surface",
             {}},
        };
        for (const auto& texture : engine_texture_assets())
            result.push_back({texture.id, TextureAsset::type, texture.name, {}});
        return result;
    }();
    return values;
}
inline const EngineAsset* engine_asset(AssetId id) {
    for (const auto& asset : engine_assets())
        if (asset.id == id)
            return &asset;
    return nullptr;
}
inline AssetRef<MeshAsset> engine_primitive(unsigned kind) {
    for (const auto& asset : engine_assets())
        if (asset.primitive == kind)
            return {asset.id};
    throw std::runtime_error("No built-in mesh for this primitive kind");
}
enum class EngineMaterial { Default, TwoSided, LegacyBlockout, Error };
inline AssetRef<MaterialAsset> engine_material(EngineMaterial kind = EngineMaterial::Default) {
    switch (kind) {
    case EngineMaterial::Default:
        return {AssetId::parse("9d087798-a173-48e0-938d-7607eed42f91")};
    case EngineMaterial::TwoSided:
        return {AssetId::parse("688cb877-0073-48bc-8fd9-141a4e28e210")};
    case EngineMaterial::Error:
        return {AssetId::parse("d60dd541-31eb-4fbf-b720-63e8ffa1702a")};
    case EngineMaterial::LegacyBlockout:
        return {AssetId::parse("39bab1fe-073f-4b6f-abd2-6ee2fc780696")};
    }
    throw std::runtime_error("Unknown engine material");
}
// Stable built-in recipe identity shared by dependency admission and resource loaders.
std::string engine_asset_revision(AssetId);
} // namespace forge
