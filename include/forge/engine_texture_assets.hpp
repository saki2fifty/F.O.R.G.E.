#pragma once
#include <forge/texture_asset.hpp>
namespace forge {
enum class EngineTexture { White, Black, FlatNormal, Checker };
struct EngineTextureAsset {
    AssetId id;
    EngineTexture kind;
    TextureDimension dimension;
    const char* name;
};
// Explicit immutable UUID allocations, not identities derived from an enum or path.
inline std::span<const EngineTextureAsset> engine_texture_assets() {
    static const EngineTextureAsset values[]{
        {AssetId::parse("af69e469-dd94-4545-a61f-682cafb574ee"), EngineTexture::White,
         TextureDimension::D2, "White / 2D"},
        {AssetId::parse("33592b60-e696-406b-b88c-4877bfa98b79"), EngineTexture::White,
         TextureDimension::D2Array, "White / 2D array"},
        {AssetId::parse("d4c60053-60c8-4456-8771-a7cf8182a902"), EngineTexture::White,
         TextureDimension::Cube, "White / Cube"},
        {AssetId::parse("d3c0aafc-a194-4cd4-b156-37427e40ddd5"), EngineTexture::White,
         TextureDimension::CubeArray, "White / Cube array"},
        {AssetId::parse("b5b38a61-92dd-4041-9a70-2d717810a75c"), EngineTexture::White,
         TextureDimension::D3, "White / 3D"},
        {AssetId::parse("28e329e6-9951-41a4-870d-c52f86e00128"), EngineTexture::Black,
         TextureDimension::D2, "Black / 2D"},
        {AssetId::parse("e2a61895-3f8a-4f4b-a017-5a7f4d5ab18b"), EngineTexture::Black,
         TextureDimension::D2Array, "Black / 2D array"},
        {AssetId::parse("4abe3e4c-b818-49fd-b230-4f0620e04cad"), EngineTexture::Black,
         TextureDimension::Cube, "Black / Cube"},
        {AssetId::parse("ea184688-aac7-4969-b707-8d1cddba59e3"), EngineTexture::Black,
         TextureDimension::CubeArray, "Black / Cube array"},
        {AssetId::parse("c206c870-4233-4bc8-b715-f0fc9497c543"), EngineTexture::Black,
         TextureDimension::D3, "Black / 3D"},
        {AssetId::parse("a3bfc1c0-77e1-4bfe-a5dd-d8f6ee2a7fb2"), EngineTexture::FlatNormal,
         TextureDimension::D2, "Flat normal / 2D"},
        {AssetId::parse("74746d6b-b0cf-41f0-852d-5e2eacf6ae51"), EngineTexture::FlatNormal,
         TextureDimension::D2Array, "Flat normal / 2D array"},
        {AssetId::parse("a0e3b4d9-8587-4f7f-89c3-4681a3b85f5f"), EngineTexture::FlatNormal,
         TextureDimension::Cube, "Flat normal / Cube"},
        {AssetId::parse("ffe50a0c-d300-422c-b72a-70a19075fb91"), EngineTexture::FlatNormal,
         TextureDimension::CubeArray, "Flat normal / Cube array"},
        {AssetId::parse("9d5975e9-8cc4-48de-9952-4d74fe0636d8"), EngineTexture::FlatNormal,
         TextureDimension::D3, "Flat normal / 3D"},
        {AssetId::parse("619babf9-df8f-4d18-91da-2e8096bd9464"), EngineTexture::Checker,
         TextureDimension::D2, "Checker / 2D"},
        {AssetId::parse("7af083a4-0e2c-4718-8fa2-ca66607545f4"), EngineTexture::Checker,
         TextureDimension::D2Array, "Checker / 2D array"},
        {AssetId::parse("9612320a-4529-4fed-a125-300692ebd4de"), EngineTexture::Checker,
         TextureDimension::Cube, "Checker / Cube"},
        {AssetId::parse("73e52fba-4079-46fb-bd6f-9c372726d567"), EngineTexture::Checker,
         TextureDimension::CubeArray, "Checker / Cube array"},
        {AssetId::parse("5c797b20-9bf5-4f4b-84d1-81ecda85801d"), EngineTexture::Checker,
         TextureDimension::D3, "Checker / 3D"},
    };
    return values;
}
inline const EngineTextureAsset* engine_texture_asset(AssetId id) {
    for (const auto& value : engine_texture_assets())
        if (value.id == id)
            return &value;
    return nullptr;
}
inline AssetRef<TextureAsset> engine_texture(EngineTexture kind,
                                             TextureDimension dimension = TextureDimension::D2) {
    for (const auto& value : engine_texture_assets())
        if (value.kind == kind && value.dimension == dimension)
            return {value.id};
    throw std::runtime_error("Unknown engine texture kind or dimension");
}
} // namespace forge
