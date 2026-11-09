#include "ui_shaped_font.hpp"
#include "ui_font_shaping.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StringUtilities.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <algorithm>
#include <cmath>
#include <hb-ot.h>
#include <hb.h>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace forge {
namespace {
constexpr std::size_t font_bytes_limit = 16 * 1024 * 1024;
constexpr std::size_t atlas_bytes_limit = 32 * 1024 * 1024;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
using HbFace = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>;
using HbFont = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
struct Face {
    std::string bytes, family;
    FT_Face native = nullptr;
    HbFace shaping{nullptr, hb_face_destroy};
    Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal;
    int weight = 400;
    bool fallback = false;
    ~Face() {
        if (native)
            FT_Done_Face(native);
    }
};
struct SizedFace {
    Face* face = nullptr;
    int size = 0;
    Rml::FontMetrics metrics{};
    HbFont shaping{nullptr, hb_font_destroy};
    Rml::FontFaceHandle id = 0;
    bool warmed = false;
};
struct Page {
    int side = 0;
    Rml::CallbackTextureSource texture;
};
struct Glyph {
    Page* page = nullptr;
    Rml::Vector2i origin{}, dimensions{}, uv{};
};
using GlyphKey = std::tuple<Rml::FontFaceHandle, std::uint32_t, const Rml::FontEffect*>;
struct Bitmap {
    GlyphKey key;
    Glyph glyph;
    std::vector<Rml::byte> rgba;
};

// One opt-in font backend, using only public layout/raster/shaping interfaces.
class ShapedFont final : public Rml::FontEngineInterface {
    std::string& diagnostic_;
    FT_Library library_ = nullptr;
    std::vector<std::unique_ptr<Face>> faces_;
    std::map<Rml::FontFaceHandle, std::unique_ptr<SizedFace>> sized_;
    std::map<Rml::FontEffectsHandle, Rml::FontEffectList> effects_;
    std::map<GlyphKey, Glyph> glyphs_;
    std::vector<std::unique_ptr<Page>> pages_;
    std::size_t source_bytes_ = 0, atlas_bytes_ = 0;
    std::uintptr_t next_face_ = 1, next_effect_ = 1;
    int version_ = 1;

    void advance_version() {
        version_ = version_ == std::numeric_limits<int>::max() ? 1 : version_ + 1;
    }

    template <class T, class F> T guarded(T fallback, F operation) {
        try {
            return operation();
        } catch (const std::exception& error) {
            if (diagnostic_.empty())
                diagnostic_ = "UI shaped text: " + std::string(error.what());
        } catch (...) {
            if (diagnostic_.empty())
                diagnostic_ = "UI shaped text: Native font operation failed";
        }
        return fallback;
    }
    SizedFace& sized(Rml::FontFaceHandle id) {
        const auto found = sized_.find(id);
        require(found != sized_.end(), "Font handle is no longer available");
        return *found->second;
    }
    void pixels(SizedFace& face) {
        require(FT_Set_Pixel_Sizes(face.face->native, 0, unsigned(face.size)) == 0,
                "Cannot select font pixel size");
    }
    Rml::FontFaceHandle size(Face& face, int pixels_count) {
        require(pixels_count >= 1 && pixels_count <= 512, "Shaped font size must be1..512px");
        for (const auto& [id, value] : sized_)
            if (value->face == &face && value->size == pixels_count)
                return id;
        require(sized_.size() < 128, "Shaped sized-font budget exceeded; restart Play");
        auto value = std::make_unique<SizedFace>();
        value->face = &face;
        value->size = pixels_count;
        value->id = next_face_++;
        pixels(*value);
        value->shaping.reset(hb_font_create(face.shaping.get()));
        require(value->shaping.get() != hb_font_get_empty(), "Cannot allocate sized shaping font");
        hb_ot_font_set_funcs(value->shaping.get());
        hb_font_set_scale(value->shaping.get(), pixels_count * 64, pixels_count * 64);
        auto& metrics = value->metrics;
        metrics.size = pixels_count;
        metrics.ascent = face.native->size->metrics.ascender / 64.f;
        metrics.descent = -face.native->size->metrics.descender / 64.f;
        metrics.line_spacing = face.native->size->metrics.height / 64.f;
        metrics.x_height = pixels_count * .5f;
        if (FT_Load_Char(face.native, 'x', FT_LOAD_DEFAULT) == 0)
            metrics.x_height = face.native->glyph->metrics.height / 64.f;
        metrics.underline_position =
            -FT_MulFix(face.native->underline_position, face.native->size->metrics.y_scale) / 64.f;
        metrics.underline_thickness = std::max(
            1.f,
            FT_MulFix(face.native->underline_thickness, face.native->size->metrics.y_scale) / 64.f);
        metrics.has_ellipsis = FT_Get_Char_Index(face.native, 0x2026) != 0;
        require(std::isfinite(metrics.ascent) && std::isfinite(metrics.descent) &&
                    std::isfinite(metrics.line_spacing) && std::isfinite(metrics.x_height) &&
                    metrics.ascent >= 0 && metrics.ascent <= 4096 && metrics.descent >= 0 &&
                    metrics.descent <= 4096 && metrics.line_spacing >= 0 &&
                    metrics.line_spacing <= 8192 && metrics.x_height >= 0 &&
                    metrics.x_height <= 4096 && std::abs(metrics.underline_position) <= 4096 &&
                    metrics.underline_thickness <= 4096,
                "Shaped font metrics exceed the profile");
        const auto id = value->id;
        sized_.emplace(id, std::move(value));
        return id;
    }
    std::pair<SizedFace*, UiShapedRun> run(Rml::FontFaceHandle id, Rml::StringView text,
                                           const Rml::TextShapingContext& context) {
        auto& primary = sized(id);
        const auto source =
            text.empty() ? std::string_view{} : std::string_view(text.begin(), text.size());
        auto shaped = shape_ui_font_run(primary.shaping.get(), source, context);
        const auto covered = [](const UiShapedRun& value) {
            return std::ranges::all_of(value.glyphs,
                                       [](const auto& glyph) { return glyph.index != 0; });
        };
        if (!covered(shaped))
            for (const auto& face : faces_)
                if (face->fallback && face.get() != primary.face) {
                    auto& fallback = sized(size(*face, primary.size));
                    auto candidate = shape_ui_font_run(fallback.shaping.get(), source, context);
                    if (covered(candidate))
                        return {&fallback, std::move(candidate)};
                }
        return {&primary, std::move(shaped)};
    }
    bool load(Rml::Span<const Rml::byte> data, int index, Rml::String family,
              Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback) {
        require(index == 0 && !data.empty() && data.size() <= 4 * 1024 * 1024,
                "Shaped text requires a standalone font face within4MiB");
        require(library_, "Shaped font engine is not initialized");
        const std::string bytes(reinterpret_cast<const char*>(data.data()), data.size());
        family = Rml::StringUtilities::ToLower(std::move(family));
        for (const auto& face : faces_)
            if (face->bytes == bytes && (family.empty() || face->family == family) &&
                (weight == Rml::Style::FontWeight::Auto || face->weight == int(weight)) &&
                (family.empty() || face->style == style)) {
                face->fallback = face->fallback || fallback;
                return true;
            }
        require(faces_.size() < 32 && source_bytes_ + bytes.size() <= font_bytes_limit,
                "Shaped font source budget exceeded; restart Play");
        auto face = std::make_unique<Face>();
        face->bytes = bytes;
        require(FT_New_Memory_Face(library_, reinterpret_cast<const FT_Byte*>(face->bytes.data()),
                                   FT_Long(face->bytes.size()), 0, &face->native) == 0 &&
                    FT_IS_SCALABLE(face->native),
                "Font does not provide a scalable face");
        require(FT_Select_Charmap(face->native, FT_ENCODING_UNICODE) == 0,
                "Font has no Unicode character map");
        if (family.empty()) {
            require(face->native->family_name != nullptr, "Font has no family name");
            family = Rml::StringUtilities::ToLower(face->native->family_name);
            style = (face->native->style_flags & FT_STYLE_FLAG_ITALIC)
                        ? Rml::Style::FontStyle::Italic
                        : Rml::Style::FontStyle::Normal;
        }
        face->family = std::move(family);
        face->style = style;
        face->weight = int(weight);
        if (weight == Rml::Style::FontWeight::Auto) {
            const auto* os2 =
                static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face->native, ft_sfnt_os2));
            face->weight = os2 && os2->usWeightClass >= 1 && os2->usWeightClass <= 1000
                               ? os2->usWeightClass
                           : (face->native->style_flags & FT_STYLE_FLAG_BOLD) ? 700
                                                                              : 400;
        }
        std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)> blob(
            hb_blob_create_or_fail(face->bytes.data(), unsigned(face->bytes.size()),
                                   HB_MEMORY_MODE_READONLY, nullptr, nullptr),
            hb_blob_destroy);
        require(bool(blob), "Cannot allocate shaping font source");
        face->shaping.reset(hb_face_create_or_fail(blob.get(), 0));
        require(bool(face->shaping) && hb_face_get_glyph_count(face->shaping.get()),
                "Cannot open shaping font tables");
        face->fallback = fallback;
        faces_.push_back(std::move(face));
        source_bytes_ += bytes.size();
        advance_version();
        return true;
    }
    bool load_file(const Rml::String& path, int index, const Rml::String& family,
                   Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallback) {
        auto* files = Rml::GetFileInterface();
        require(files != nullptr, "Font source admission is unavailable");
        Rml::String bytes;
        require(files->LoadFile(path, bytes), "Cannot read admitted font source");
        return load({reinterpret_cast<const Rml::byte*>(bytes.data()), bytes.size()}, index, family,
                    style, weight, fallback);
    }
    Bitmap bitmap(SizedFace& face, std::uint32_t index, const Rml::FontEffect* effect) {
        pixels(face);
        require(FT_Load_Glyph(face.face->native, index, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) == 0 &&
                    FT_Render_Glyph(face.face->native->glyph, FT_RENDER_MODE_NORMAL) == 0,
                "Cannot rasterize shaped glyph");
        const auto slot = face.face->native->glyph;
        const auto& source = slot->bitmap;
        require(source.width <= 1022 && source.rows <= 1022, "Shaped glyph bitmap exceeds1022px");
        Bitmap result;
        result.key = {face.id, index, effect};
        result.glyph.origin = {slot->bitmap_left, -slot->bitmap_top};
        result.glyph.dimensions = {int(source.width), int(source.rows)};
        if (!source.width || !source.rows)
            return result;
        require(source.buffer && (source.pixel_mode == FT_PIXEL_MODE_GRAY ||
                                  source.pixel_mode == FT_PIXEL_MODE_MONO),
                "Shaped text requires grayscale or monochrome outlines");
        const auto stride = std::abs(source.pitch);
        require(stride >= int(source.pixel_mode == FT_PIXEL_MODE_MONO ? (source.width + 7) / 8
                                                                      : source.width),
                "Invalid shaped glyph bitmap stride");
        std::vector<Rml::byte> alpha(std::size_t(source.width) * source.rows);
        for (unsigned y = 0; y < source.rows; ++y) {
            const auto* row =
                source.buffer + std::size_t(source.pitch < 0 ? source.rows - 1 - y : y) * stride;
            for (unsigned x = 0; x < source.width; ++x)
                alpha[std::size_t(y) * source.width + x] =
                    source.pixel_mode == FT_PIXEL_MODE_MONO
                        ? ((row[x / 8] & (0x80 >> (x % 8))) ? 255 : 0)
                        : row[x];
        }
        Rml::FontGlyph glyph;
        glyph.bearing = {slot->bitmap_left, slot->bitmap_top};
        glyph.advance = int(std::lround(slot->advance.x / 64.));
        glyph.bitmap_data = alpha.data();
        glyph.bitmap_dimensions = result.glyph.dimensions;
        glyph.color_format = Rml::ColorFormat::A8;
        Rml::Vector2i offset{};
        if (effect && !effect->GetGlyphMetrics(offset, result.glyph.dimensions, glyph)) {
            result.glyph.dimensions = {};
            return result;
        }
        result.glyph.origin += offset;
        const auto dimensions = result.glyph.dimensions;
        require(dimensions.x >= 0 && dimensions.y >= 0 && dimensions.x <= 1022 &&
                    dimensions.y <= 1022,
                "Font effect exceeds the shaped glyph profile");
        result.rgba.resize(std::size_t(dimensions.x) * dimensions.y * 4);
        if (effect)
            effect->GenerateGlyphTexture(result.rgba.data(), dimensions, dimensions.x * 4, glyph);
        else
            for (std::size_t i = 0; i < alpha.size(); ++i)
                std::fill_n(result.rgba.data() + i * 4, 4, alpha[i]);
        return result;
    }
    void ensure(SizedFace& face, const UiShapedRun& run, const Rml::FontEffect* effect) {
        if (effect && !effect->HasUniqueTexture())
            effect = nullptr;
        std::set<std::uint32_t> missing;
        for (const auto& glyph : run.glyphs)
            if (!glyphs_.contains({face.id, glyph.index, effect}))
                missing.insert(glyph.index);
        const bool warm = !effect && !face.warmed;
        if (warm)
            for (std::uint32_t c = 32; c < 127; ++c) {
                hb_codepoint_t index = 0;
                if (hb_font_get_nominal_glyph(face.shaping.get(), c, &index) &&
                    !glyphs_.contains({face.id, index, nullptr}))
                    missing.insert(index);
            }
        require(glyphs_.size() + missing.size() <= 16384,
                "Shaped glyph cache exceeded16384 entries");
        std::vector<Bitmap> pending;
        std::size_t area = 0, transient = 0;
        int largest = 0;
        for (const auto index : missing) {
            auto value = bitmap(face, index, effect);
            transient += value.rgba.size();
            require(transient <= atlas_bytes_limit, "Shaped glyph preparation exceeds32MiB");
            area += std::size_t(value.glyph.dimensions.x + 2) * (value.glyph.dimensions.y + 2);
            largest =
                std::max({largest, value.glyph.dimensions.x + 2, value.glyph.dimensions.y + 2});
            pending.push_back(std::move(value));
        }
        int side = 64;
        while (side < largest || (side < 512 && std::size_t(side) * side < area))
            side *= 2;
        require(side <= 1024, "Shaped glyph atlas side exceeds1024px");
        std::vector<std::unique_ptr<Page>> pages;
        std::shared_ptr<std::vector<Rml::byte>> pixels;
        int x = 1, y = 1, row_height = 0;
        for (auto& value : pending) {
            const auto dimensions = value.glyph.dimensions;
            if (!dimensions.x || !dimensions.y)
                continue;
            if (x + dimensions.x + 1 > side) {
                x = 1;
                y += row_height + 1;
                row_height = 0;
            }
            if (!pixels || y + dimensions.y + 1 > side) {
                require(pages_.size() + pages.size() < 128 &&
                            atlas_bytes_ + (pages.size() + 1) * std::size_t(side) * side * 4 <=
                                atlas_bytes_limit,
                        "Shaped glyph atlases exceed128 pages/32MiB; restart Play");
                auto page = std::make_unique<Page>();
                page->side = side;
                pixels = std::make_shared<std::vector<Rml::byte>>(std::size_t(side) * side * 4);
                page->texture = Rml::CallbackTextureSource(
                    [pixels, side](const Rml::CallbackTextureInterface& target) {
                        return target.GenerateTexture(*pixels, {side, side});
                    });
                pages.push_back(std::move(page));
                x = y = 1;
                row_height = 0;
            }
            value.glyph.page = pages.back().get();
            value.glyph.uv = {x, y};
            for (int row = 0; row < dimensions.y; ++row)
                std::copy_n(value.rgba.data() + std::size_t(row) * dimensions.x * 4,
                            std::size_t(dimensions.x) * 4,
                            pixels->data() + (std::size_t(y + row) * side + x) * 4);
            x += dimensions.x + 1;
            row_height = std::max(row_height, dimensions.y);
        }
        // Published pages stay immutable: existing native geometry keeps its UVs/textures.
        for (auto& page : pages) {
            atlas_bytes_ += std::size_t(page->side) * page->side * 4;
            pages_.push_back(std::move(page));
        }
        for (auto& value : pending)
            glyphs_.emplace(value.key, value.glyph);
        if (warm)
            face.warmed = true;
    }
    void layer(Rml::RenderManager& manager, SizedFace& face, const UiShapedRun& run,
               Rml::Vector2f position, Rml::ColourbPremultiplied colour,
               const Rml::FontEffect* effect, Rml::TexturedMeshList& meshes) {
        ensure(face, run, effect);
        std::map<Page*, std::size_t> groups;
        for (const auto& shaped : run.glyphs) {
            const auto& glyph = glyphs_.at(
                {face.id, shaped.index, effect && effect->HasUniqueTexture() ? effect : nullptr});
            if (!glyph.page)
                continue;
            auto [entry, added] = groups.emplace(glyph.page, meshes.size());
            if (added) {
                meshes.emplace_back();
                meshes.back().texture = glyph.page->texture.GetTexture(manager);
            }
            auto origin = glyph.origin;
            if (effect && !effect->HasUniqueTexture()) {
                Rml::FontGlyph metrics;
                metrics.bitmap_dimensions = glyph.dimensions;
                Rml::Vector2i offset{}, dimensions = glyph.dimensions;
                if (!effect->GetGlyphMetrics(offset, dimensions, metrics))
                    continue;
                origin += offset;
            }
            const auto scale = 1.f / glyph.page->side;
            Rml::MeshUtilities::GenerateQuad(
                meshes.at(entry->second).mesh,
                position + Rml::Vector2f{shaped.x + origin.x, shaped.y + origin.y},
                Rml::Vector2f(glyph.dimensions), colour, Rml::Vector2f(glyph.uv) * scale,
                Rml::Vector2f(glyph.uv + glyph.dimensions) * scale);
        }
    }

  public:
    explicit ShapedFont(std::string& diagnostic) : diagnostic_(diagnostic) {}
    void Initialize() override {
        (void)guarded(false, [&] {
            if (!library_)
                require(FT_Init_FreeType(&library_) == 0,
                        "Cannot initialize shaped-font rasterization");
            return true;
        });
    }
    ~ShapedFont() override { Shutdown(); }
    void Shutdown() override {
        ReleaseFontResources();
        faces_.clear();
        source_bytes_ = 0;
        if (library_)
            FT_Done_FreeType(std::exchange(library_, nullptr));
    }
    bool LoadFontFace(const Rml::String& path, int index, bool fallback,
                      Rml::Style::FontWeight weight) override {
        return guarded(false, [&] {
            return load_file(path, index, {}, Rml::Style::FontStyle::Normal, weight, fallback);
        });
    }
    bool LoadFontFace(const Rml::String& path, int index, const Rml::String& family,
                      Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                      bool fallback) override {
        return guarded(false,
                       [&] { return load_file(path, index, family, style, weight, fallback); });
    }
    bool LoadFontFace(Rml::Span<const Rml::byte> data, int index, const Rml::String& family,
                      Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                      bool fallback) override {
        return guarded(false, [&] { return load(data, index, family, style, weight, fallback); });
    }
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                          Rml::Style::FontWeight weight,
                                          int pixels_count) override {
        return guarded(Rml::FontFaceHandle{}, [&] {
            Rml::StringList names;
            Rml::StringUtilities::ExpandString(names, Rml::StringUtilities::ToLower(family), ',',
                                               '"', '"', true);
            for (auto name : names) {
                if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'')
                    name = name.substr(1, name.size() - 2);
                Face* selected = nullptr;
                int distance = std::numeric_limits<int>::max();
                for (const auto& face : faces_)
                    if (face->family == name) {
                        const int score = (face->style == style ? 0 : 2000) +
                                          std::abs(face->weight - int(weight));
                        if (score < distance) {
                            distance = score;
                            selected = face.get();
                        }
                    }
                if (selected)
                    return size(*selected, pixels_count);
            }
            return Rml::FontFaceHandle{};
        });
    }
    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                              const Rml::FontEffectList& effects) override {
        return guarded(Rml::FontEffectsHandle{}, [&] {
            (void)sized(handle);
            if (effects.empty())
                return Rml::FontEffectsHandle{};
            require(effects.size() <= 16, "Shaped text supports16 effects per layer group");
            require(std::ranges::all_of(effects, [](const auto& value) { return bool(value); }),
                    "Font effect is unavailable");
            for (const auto& [id, value] : effects_)
                if (value == effects)
                    return id;
            require(effects_.size() < 64, "Shaped text effect cache exceeded64 groups");
            const auto id = next_effect_++;
            effects_.emplace(id, effects);
            return Rml::FontEffectsHandle(id);
        });
    }
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override {
        static const Rml::FontMetrics empty{};
        return *guarded(
            &empty, [&] { return static_cast<const Rml::FontMetrics*>(&sized(handle).metrics); });
    }
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView text,
                       const Rml::TextShapingContext& context, Rml::Character prior) override {
        return guarded(0, [&] {
            auto [selected, shaped] = run(handle, text, context);
            auto& face = *selected;
            if (prior != Rml::Character::Null && !shaped.glyphs.empty() &&
                context.font_kerning != Rml::Style::FontKerning::None) {
                pixels(face);
                FT_Vector offset{};
                if (FT_Get_Kerning(face.face->native,
                                   FT_Get_Char_Index(face.face->native, FT_ULong(prior)),
                                   shaped.glyphs.front().index, FT_KERNING_DEFAULT, &offset) == 0)
                    shaped.advance += offset.x / 64.f;
            }
            return int(std::lround(std::max(0.f, shaped.advance)));
        });
    }
    int GenerateString(Rml::RenderManager& manager, Rml::FontFaceHandle handle,
                       Rml::FontEffectsHandle effects, Rml::StringView text, Rml::Vector2f position,
                       Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& context,
                       Rml::TexturedMeshList& meshes) override {
        return guarded(0, [&] {
            require(std::isfinite(opacity) && opacity >= 0 && opacity <= 1,
                    "Invalid shaped text opacity");
            auto [selected, shaped] = run(handle, text, context);
            auto& face = *selected;
            if (shaped.glyphs.empty())
                return 0;
            Rml::TexturedMeshList candidate;
            const Rml::FontEffectList empty;
            const auto& list = effects ? effects_.at(effects) : empty;
            for (const auto& effect : list)
                if (effect->GetLayer() == Rml::FontEffect::Layer::Back)
                    layer(manager, face, shaped, position,
                          effect->GetColour().ToPremultiplied(opacity), effect.get(), candidate);
            layer(manager, face, shaped, position, colour, nullptr, candidate);
            for (const auto& effect : list)
                if (effect->GetLayer() == Rml::FontEffect::Layer::Front)
                    layer(manager, face, shaped, position,
                          effect->GetColour().ToPremultiplied(opacity), effect.get(), candidate);
            std::erase_if(candidate, [](const auto& value) { return !value.mesh; });
            meshes.insert(meshes.end(), std::make_move_iterator(candidate.begin()),
                          std::make_move_iterator(candidate.end()));
            return int(std::lround(std::max(0.f, shaped.advance)));
        });
    }
    int GetVersion(Rml::FontFaceHandle handle) override {
        return guarded(0, [&] {
            (void)sized(handle);
            return version_;
        });
    }
    void ReleaseFontResources() override {
        glyphs_.clear();
        pages_.clear();
        effects_.clear();
        sized_.clear();
        atlas_bytes_ = 0;
        advance_version();
    }
};
} // namespace
std::unique_ptr<Rml::FontEngineInterface> make_ui_shaped_font(std::string& diagnostic) {
    return std::make_unique<ShapedFont>(diagnostic);
}
} // namespace forge
