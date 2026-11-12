#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include "model_render_resource.hpp"
#include "ui_filter_admission.hpp"
#include "ui_gradient_admission.hpp"
#include "ui_inspection_transport.hpp"
#include "ui_material_effect.hpp"
#include <RmlUi/Core.h>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/ui_presenter.hpp>
#include <set>
namespace {
// Native parsing/layout/resource admission only. Geometry is bounded and retired
// without allocating GPU objects; graphical readiness remains the game host's job.
struct InspectionRenderer final : Rml::RenderInterface {
    std::filesystem::path project;
    forge::AssetCatalog catalog;
    std::set<forge::AssetId> admitted_materials;
    explicit InspectionRenderer(std::filesystem::path root)
        : project(std::move(root)), catalog(forge::AssetCatalog::open_project(project)) {}
    std::uintptr_t next = 1;
    std::set<std::uintptr_t> geometry, textures, filters, gradients;
    Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override {
        if (name == "shader") {
            const auto effect = forge::ui_effect_detail::admit_material_effect(parameters);
            if (!admitted_materials.contains(effect.material.id)) {
                const auto material =
                    forge::asset_detail::load_pbr_material(project, catalog, effect.material);
                forge::validate_ui_effect_material(material);
                admitted_materials.insert(effect.material.id);
            }
        } else
            (void)forge::ui_gradient_detail::admit_gradient(name, parameters);
        if (gradients.size() >= forge::ui_gradient_detail::max_shaders)
            throw std::runtime_error("export.ui.limit: Compiled shader count exceeds 256");
        const auto id = next++;
        gradients.insert(id);
        return id;
    }
    void ReleaseShader(Rml::CompiledShaderHandle shader) override { gradients.erase(shader); }
    void RenderShader(Rml::CompiledShaderHandle, Rml::CompiledGeometryHandle, Rml::Vector2f,
                      Rml::TextureHandle) override {}
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override {
        (void)forge::ui_filter_detail::admit_filter(name, parameters);
        if (filters.size() >= forge::ui_filter_detail::max_filters)
            throw std::runtime_error("export.ui.limit: Compiled filter count exceeds 256");
        const auto id = next++;
        filters.insert(id);
        return id;
    }
    void ReleaseFilter(Rml::CompiledFilterHandle h) override { filters.erase(h); }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
        if (filters.size() >= forge::ui_filter_detail::max_filters)
            throw std::runtime_error("export.ui.limit: Compiled filter count exceeds 256");
        const auto id = next++;
        filters.insert(id);
        return id;
    }
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override {
        if (geometry.size() >= 32768 || vertices.size() > 1000000 || indices.size() > 3000000)
            throw std::runtime_error("export.ui.limit: Geometry inspection budget exceeded");
        const auto id = next++;
        geometry.insert(id);
        return id;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle h) override { geometry.erase(h); }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& path) override {
        Rml::String data;
        if (!Rml::GetFileInterface()->LoadFile(path, data))
            return 0;
        const auto image = forge::decode_ui_image(std::as_bytes(std::span(data)));
        dimensions = {int(image.width), int(image.height)};
        return GenerateTexture({}, dimensions);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        if (textures.size() >= 256)
            throw std::runtime_error("export.ui.limit: Texture inspection budget exceeded");
        const auto id = next++;
        textures.insert(id);
        return id;
    }
    void ReleaseTexture(Rml::TextureHandle h) override { textures.erase(h); }
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    void SetTransform(const Rml::Matrix4f*) override {}
    void EnableClipMask(bool) override {}
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle,
                          Rml::Vector2f) override {}
};
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    if (argc != 2 || std::string_view(argv[1]) != "--inspect-ui-worker")
        return 2;
    try {
        const auto input = asset_detail::parse_bounded_json(
            asset_detail::read_bytes("request.json", 65536), 65536, 4096, 8);
        const auto root = std::filesystem::u8path(input.at("project").get<std::string>());
        const auto font = std::filesystem::u8path(input.at("font").get<std::string>());
        if (!root.is_absolute() || !font.is_absolute())
            throw std::runtime_error(
                "export.ui.inspection: Absolute trusted worker paths required");
        InspectionRenderer renderer(root);
        const auto settings = ProjectSettings(root).document();
        UiPresenter presenter(
            renderer, root, asset_detail::read_bytes(font, 4 * 1024 * 1024), {}, nullptr,
            settings.contains("game") && ui_text_shaping_enabled(settings.at("game")),
            settings.contains("game") && ui_grapheme_editing_enabled(settings.at("game")));
        const auto result = presenter.inspect_static({input.at("asset").get<AssetId>()});
        asset_storage::replace(std::filesystem::current_path() / "result.json",
                               ui_inspection_detail::encode(result).dump());
        return 0;
    } catch (const std::exception& e) {
        try {
            asset_storage::replace(
                "error.json",
                nlohmann::json{{"message", std::string(e.what()).substr(0, 4096)}}.dump());
        } catch (...) {
        }
        return 1;
    }
}
