#pragma once
#include "material_authoring.hpp"
#include "material_document.hpp"
#include "render_texture_authoring.hpp"

namespace test_render_texture {
template <class Document, class Prepare>
void publish(const std::shared_ptr<const forge::ProjectLease>& lease, Document& document,
             std::shared_ptr<const forge::AssetImporterRegistry> registry, Prepare prepare) {
    document.save();
    forge::AssetImportService imports(lease, std::move(registry), {"windows", "d3d12", "desktop"},
                                      1);
    imports.submit(
        imports.prepare(document.locator(), {}, document.source().asset()),
        [prepare](auto& candidate, const auto& plan, const auto&) { prepare(candidate, plan); },
        [](const auto&, const auto&) {});
    if (!imports.wait_idle(std::chrono::seconds(20)))
        throw std::runtime_error("Camera image fixture publication timed out");
    const auto outcomes = imports.poll();
    if (outcomes.size() != 1 || !outcomes.front().published)
        throw std::runtime_error(outcomes.empty() ? "Missing camera image publication receipt"
                                                  : outcomes.front().diagnostic);
}
inline std::unique_ptr<forge::RenderTextureDocument>
output(const std::shared_ptr<forge::ProjectLease>& lease, const std::filesystem::path& path) {
    auto doc = forge::RenderTextureDocument::create(lease, path, [](forge::AssetId id) {
        auto value = forge::RenderTextureSource::create(id);
        value.document["width"] = 32;
        value.document["height"] = 16;
        return value;
    });
    publish(lease, *doc, forge::render_texture_import_registry(),
            forge::prepare_render_texture_publication);
    return doc;
}
inline forge::AssetId material(const std::shared_ptr<forge::ProjectLease>& lease,
                               const std::filesystem::path& path, forge::AssetId image = {}) {
    auto doc = forge::MaterialDocument::create(lease, path, [=](forge::AssetId id) {
        auto value = forge::MaterialSource::create(id);
        auto& overrides = value.document["overrides"];
        overrides["model"] = "forge.gltf.unlit.v1";
        overrides["double_sided"] = true;
        if (image) {
            value.enable_render_textures();
            forge::MaterialData shape;
            shape.model = "forge.gltf.unlit.v1";
            shape.textures["baseColorTexture"].semantic = forge::TextureSemantic::Color;
            overrides["textures"]["baseColorTexture"] = {
                {"render_texture", image},
                {"slot",
                 forge::material_values_document(shape).at("textures").at("baseColorTexture")}};
        } else {
            overrides["parameters"]["baseColorFactor"] = {
                {"type", unsigned(forge::MaterialParameterType::LinearColor4)},
                {"value", {.125f, .5f, .25f, 1.f}}};
        }
        return value;
    });
    publish(lease, *doc, forge::material_import_registry(), forge::prepare_material_publication);
    return doc->source().asset();
}
} // namespace test_render_texture
