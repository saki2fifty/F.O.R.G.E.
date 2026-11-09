#include "asset_bytes.hpp"
#include "fixtures/ui_font_collection.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <algorithm>
#include <array>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class MaskEffect : public Rml::FontEffect {
  public:
    bool GetGlyphMetrics(Rml::Vector2i&, Rml::Vector2i&, const Rml::FontGlyph&) const override {
        return true;
    }
};
class CoverageEffect final : public MaskEffect {
  public:
    mutable bool partial_alpha = false;
    bool HasUniqueTexture() const override { return true; }
    void GenerateGlyphTexture(Rml::byte* target, Rml::Vector2i dimensions, int stride,
                              const Rml::FontGlyph& glyph) const override {
        check(glyph.color_format == Rml::ColorFormat::A8 && glyph.bitmap_data &&
                  dimensions == glyph.bitmap_dimensions,
              "Color font effect did not receive intrinsic alpha coverage");
        for (int y = 0; y < dimensions.y; ++y)
            for (int x = 0; x < dimensions.x; ++x) {
                const auto alpha = glyph.bitmap_data[std::size_t(y) * dimensions.x + x];
                partial_alpha |= alpha > 0 && alpha < 255;
                std::fill_n(target + y * stride + x * 4, 4, alpha);
            }
    }
};
std::vector<std::byte> repeated_layers(std::vector<std::byte> bytes, unsigned count) {
    using forge::test::font_u32;
    const auto u16 = [&](std::size_t at, unsigned value) {
        bytes.at(at) = std::byte(value >> 8);
        bytes.at(at + 1) = std::byte(value & 255);
    };
    std::size_t entry = 0;
    const auto tables = font_u32(bytes, 4) >> 16;
    for (unsigned i = 0; i < tables; ++i)
        if (font_u32(bytes, 12 + i * 16) == 0x434f4c52)
            entry = 12 + i * 16;
    check(entry && font_u32(bytes, entry + 12) == 32, "Original COLR fixture layout changed");
    const auto offset = font_u32(bytes, entry + 8), end = offset + 32;
    bytes.insert(bytes.begin() + end, 120, std::byte{});
    for (unsigned i = 0; i < tables; ++i) {
        const auto at = 12 + i * 16 + 8;
        const auto start = font_u32(bytes, at);
        if (start >= end)
            font_u32(bytes, at, start + 120);
    }
    font_u32(bytes, entry + 12, 152);
    u16(offset + 12, 33);
    u16(offset + 18, count);
    for (unsigned i = 0; i < 33; ++i) {
        u16(offset + 20 + i * 4, 3);
        u16(offset + 22 + i * 4, 0);
    }
    return bytes; // Native decoding is checked; stale checksums are not authenticated.
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& latin,
              const std::filesystem::path& fixture, bool foreground, bool wide) {
    using namespace forge;
    std::filesystem::create_directories(root / "Assets");
    std::filesystem::copy_file(fixture, root / "Assets/Color.ttf");
    std::ofstream(root / "Assets/Hud.rml") << R"rml(<rml><head><style>
@font-face {font-family:Color;src:"Assets/Color.ttf";}
body {font-family:Color;font-size:100px;color:#fff;}
</style></head><body><p>H</p></body></rml>)rml";
    const auto asset = register_ui_document(root, "Assets/Hud.rml");
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, latin, {}, nullptr, true);
        const auto observed = presenter.inspect_static({asset.id});
        check(observed.sources.size() == 2 && presenter.diagnostic().empty(),
              "Native color source bypassed admitted resource observation");
        const auto entity = EntityId::generate();
        presenter.reset("color-native", 1);
        const nlohmann::json snapshot{
            {"version", 1},
            {"session", "color-native"},
            {"generation", 1u},
            {"revision", 1u},
            {"documents", nlohmann::json::array({{{"entity", entity},
                                                  {"asset", asset.id},
                                                  {"instance", entity.str()},
                                                  {"visible", true},
                                                  {"layer", 0u},
                                                  {"model", nlohmann::json::object()},
                                                  {"commands", nlohmann::json::array()}}})}};
        const bool accepted = presenter.accept(snapshot);
        check(accepted, presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        auto* font = Rml::GetFontEngineInterface();
        auto* context = Rml::GetContext(0);
        check(font && context, "Native color font context is unavailable");
        const auto handle =
            font->GetFontFaceHandle("Color", Rml::Style::FontStyle::Normal,
                                    Rml::Style::FontWeight::Normal, wide ? 512 : 100);
        check(handle != 0, "Color face failed native admission");
        const Rml::String language = "en";
        const Rml::TextShapingContext shaping{language};
        const std::array<Rml::Colourb, 3> palette{
            {{240, 60, 40, 255}, {40, 150, 230, 180}, {30, 190, 100, 255}}};
        const auto generate = [&](Rml::ColourbPremultiplied caller,
                                  Rml::FontEffectsHandle effects = 0) {
            Rml::TexturedMeshList meshes;
            const auto width =
                font->GenerateString(context->GetRenderManager(), handle, effects, "H", {}, caller,
                                     caller.alpha / 255.f, shaping, meshes);
            check(width == font->GetStringWidth(handle, "H", shaping) && width > 0,
                  "Color glyph rendering changed its shaped advance");
            check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
            return meshes;
        };
        const auto verify = [&](Rml::ColourbPremultiplied caller) {
            const auto meshes = generate(caller);
            std::size_t layer = 0;
            for (const auto& mesh : meshes) {
                const auto& vertices = mesh.mesh.vertices;
                check(vertices.size() % 4 == 0, "Color layer mesh has incomplete quads");
                for (std::size_t at = 0; at < vertices.size(); at += 4) {
                    check(layer < 3, "Color glyph generated an extra layer");
                    const auto index = wide ? std::array{0u, 2u, 1u}[layer] : unsigned(layer);
                    const auto expected =
                        foreground && layer == 2
                            ? caller
                            : palette[index].ToPremultiplied(caller.alpha / 255.f);
                    for (std::size_t corner = 0; corner < 4; ++corner)
                        check(vertices[at + corner].colour == expected,
                              "Color layer order, foreground or text alpha differs");
                    ++layer;
                }
            }
            check(layer == 3, "Color glyph lost a layer or used its monochrome base");
            if (wide)
                check(meshes.size() == 3 && meshes[0].texture == meshes[2].texture &&
                          meshes[0].texture != meshes[1].texture,
                      "Alternating atlas pages reordered overlapping color layers");
        };
        verify({255, 255, 255, 255});
        const auto stable = renderer.textures.size();
        for (int repeat = 0; repeat < 4; ++repeat)
            for (unsigned alpha : {0u, 64u, 127u, 128u, 255u})
                verify({0, Rml::byte(alpha), 0, Rml::byte(alpha)});
        check(renderer.textures.size() == stable,
              "Changing foreground colors/opacity allocated glyph atlas textures");
        if (!wide) {
            auto coverage = std::make_shared<CoverageEffect>();
            coverage->SetColour({15, 25, 35, 255});
            auto shadow = std::make_shared<MaskEffect>();
            shadow->SetColour({60, 80, 100, 255});
            const auto effects = font->PrepareFontEffects(handle, {coverage, shadow});
            check(effects != 0, "Color font effects failed native preparation");
            const auto meshes = generate({255, 255, 255, 255}, effects);
            std::size_t vertices = 0;
            for (const auto& mesh : meshes)
                vertices += mesh.mesh.vertices.size();
            check(coverage->partial_alpha, "Color font effect lost intrinsic palette alpha");
            check(vertices == 20, "Color font lost its unique effect or shared shadow");
        }
        if (!wide && !foreground) {
            const auto original = asset_detail::read_bytes(fixture, 4 * 1024 * 1024);
            for (unsigned count : {32u, 33u}) {
                const auto bytes = repeated_layers(original, count);
                const auto family = "Limit" + std::to_string(count);
                check(Rml::LoadFontFace(
                          {reinterpret_cast<const Rml::byte*>(bytes.data()), bytes.size()}, family,
                          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal),
                      "Original layer-limit fixture failed native face admission");
                const auto limit = font->GetFontFaceHandle(family, Rml::Style::FontStyle::Normal,
                                                           Rml::Style::FontWeight::Normal, 100);
                check(limit != 0, "Layer-limit font handle unavailable");
                Rml::TexturedMeshList meshes;
                const auto width =
                    font->GenerateString(context->GetRenderManager(), limit, 0, "H", {},
                                         {255, 255, 255, 255}, 1, shaping, meshes);
                if (count == 32) {
                    std::size_t vertices = 0;
                    for (const auto& mesh : meshes)
                        vertices += mesh.mesh.vertices.size();
                    check(width == 60 && vertices == 128 && presenter.diagnostic().empty(),
                          "Supported32-layer glyph was rejected");
                } else
                    check(width == 0 && meshes.empty() &&
                              presenter.diagnostic().find("32 per glyph") != std::string::npos,
                          "Excess color layers published partial geometry or lacked a diagnostic");
            }
        }
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Color font resources survived presenter retirement");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 6, "Need scratch, default font and original palette/foreground/wide fonts");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto latin = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (int repeat = 0; repeat < 2; ++repeat)
            for (int fixture = 0; fixture < 3; ++fixture)
                exercise(root / forge::AssetId::generate().str(), latin, argv[fixture + 3],
                         fixture == 1, fixture == 2);
        std::cout << "Native color layers, alpha, foreground, page order, effects and retirement "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
