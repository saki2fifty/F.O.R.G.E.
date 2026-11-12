#include "ui_material_authoring.hpp"
#include "model_render_resource.hpp"

namespace forge {
std::optional<AssetId> read_ui_material_style(std::string_view text) {
    constexpr std::string_view prefix = "shader(\"material:", suffix = "\")";
    if (!text.starts_with(prefix) || !text.ends_with(suffix) ||
        text.size() != prefix.size() + 36 + suffix.size())
        return {};
    try {
        const auto id = AssetId::parse(text.substr(prefix.size(), 36));
        if (id)
            return id;
    } catch (const std::exception&) {
    }
    return {};
}
nlohmann::json ui_material_reference(const AssetCatalog& catalog, AssetId asset) {
    const auto material = asset_detail::load_pbr_material(catalog.project_root(), catalog, {asset});
    validate_ui_effect_material(material);
    return {{"asset", asset},
            {"value", "shader(\"material:" + asset.str() + "\")"},
            {"shader", material.surface->shader},
            {"shader_revision", material.surface->revision}};
}
} // namespace forge
