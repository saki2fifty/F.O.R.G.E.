#include "bounded_json.hpp"
#include <forge/render_texture.hpp>

namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::uint32_t dimension(const nlohmann::json& value) {
    require(value.is_number_integer() &&
                (value.is_number_unsigned() || value.get<std::int64_t>() >= 1) &&
                value.get<std::uint64_t>() >= 1 &&
                value.get<std::uint64_t>() <= render_texture_max_dimension,
            "Render texture dimensions must be integers from 1 to 2048");
    return value.get<std::uint32_t>();
}
} // namespace
void validate_render_texture(const RenderTextureData& value) {
    require(value.width && value.height && value.width <= render_texture_max_dimension &&
                value.height <= render_texture_max_dimension,
            "Render texture dimensions must be integers from 1 to 2048");
}
RenderTextureSource RenderTextureSource::create(AssetId id) {
    require(bool(id), "Render texture requires an asset identity");
    return {{{"format", "forge.render-texture"},
             {"version", 1},
             {"asset_id", id},
             {"profile", "linear-hdr-opaque"},
             {"width", 512},
             {"height", 512}}};
}
RenderTextureSource RenderTextureSource::parse(std::span<const std::byte> bytes) {
    RenderTextureSource result{
        asset_detail::parse_bounded_json(bytes, render_texture_source_byte_limit, 512, 8)};
    result.validate();
    return result;
}
AssetId RenderTextureSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void RenderTextureSource::validate() const {
    const auto encoded = document.dump();
    require(asset_detail::parse_bounded_json(std::as_bytes(std::span(encoded)),
                                             render_texture_source_byte_limit, 512, 8) == document,
            "Render texture must survive JSON save and reload without value changes");
    require(document.is_object() && document.at("format") == "forge.render-texture" &&
                document.at("version").is_number_integer() && document.at("version") == 1 &&
                bool(asset()) && document.at("profile") == "linear-hdr-opaque",
            "Unsupported render texture format, version, identity or profile");
    validate_render_texture({dimension(document.at("width")), dimension(document.at("height"))});
}
RenderTextureData RenderTextureSource::compiled() const {
    validate();
    return {dimension(document.at("width")), dimension(document.at("height"))};
}
std::vector<std::byte> RenderTextureSource::cook() const {
    validate();
    const auto encoded = document.dump();
    const auto bytes = std::as_bytes(std::span(encoded));
    return {bytes.begin(), bytes.end()};
}
} // namespace forge
