#pragma once
#include "image_pixels.hpp"
#include <array>
#include <cstdint>
#include <forge/texture_asset.hpp>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
namespace forge {
inline constexpr unsigned paint_texture_dimension_limit = 1024;
inline constexpr unsigned paint_texture_page_side = 32;
inline constexpr unsigned paint_texture_stroke_point_limit = 256;
inline constexpr std::size_t paint_texture_source_byte_limit = 16 * 1024 * 1024;
using PaintPixel = std::array<std::uint8_t, 4>;
struct PaintPoint {
    double x{}, y{}; // Pixel coordinates, origin at the source image's top left.
    bool operator==(const PaintPoint&) const = default;
};
struct PaintBrush {
    // RGB is linear for Color, raw normalized channels for Data. Alpha is straight
    // opacity for Color and an independent channel value for Data.
    std::array<double, 4> color{1, 1, 1, 1};
    double radius = 16, hardness = .75, opacity = 1;
    bool erase = false;
};
void validate_paint_brush(const PaintBrush&);
// Immutable pixel pages are an asset source value, not another document/history
// owner. AssetSourceDocument retains the snapshots and governs Save and Undo.
class PaintTextureValue {
  public:
    PaintTextureValue() = default;
    static PaintTextureValue create(AssetId, unsigned width = 256, unsigned height = 256,
                                    TextureSemantic = TextureSemantic::Color,
                                    PaintPixel background = {});
    static PaintTextureValue parse(nlohmann::json);
    AssetId asset() const;
    unsigned width() const;
    unsigned height() const;
    TextureSemantic semantic() const;
    PaintPixel background() const;
    const nlohmann::json& metadata() const;
    PaintPixel pixel(unsigned x, unsigned y) const;
    std::size_t page_count() const { return pages_.size(); }
    // One stroke uses maximum path coverage: overlapping samples do not build
    // opacity repeatedly inside a single gesture. Separate strokes may layer.
    PaintTextureValue paint(std::span<const PaintPoint>, const PaintBrush&,
                            std::stop_token = {}) const;
    PaintTextureValue fill(PaintPixel) const;
    PaintTextureValue replace_pixels(const Rgba8Pixels&, std::stop_token = {}) const;
    PaintTextureValue with_metadata(nlohmann::json) const;
    TextureData texture() const;
    std::string dump(int indent = -1) const;
    void validate() const;
    static std::size_t history_weight(const PaintTextureValue&, const PaintTextureValue&);
    friend bool operator==(const PaintTextureValue&, const PaintTextureValue&);
    friend void swap(PaintTextureValue& a, PaintTextureValue& b) noexcept {
        static_assert(noexcept(a.pages_.swap(b.pages_)));
        a.metadata_.swap(b.metadata_);
        a.pages_.swap(b.pages_);
        std::swap(a.encoded_bytes_, b.encoded_bytes_);
    }

  private:
    struct Metadata {
        nlohmann::json document;
        AssetId asset;
        unsigned width{}, height{};
        TextureSemantic semantic = TextureSemantic::Color;
        PaintPixel background{};
        std::size_t encoded_bytes{};
    };
    struct Page {
        std::array<PaintPixel, paint_texture_page_side * paint_texture_page_side> pixels{};
        nlohmann::json extensions = nlohmann::json::object();
        std::size_t encoded_bytes{};
    };
    std::shared_ptr<const Metadata> metadata_;
    std::map<unsigned, std::shared_ptr<const Page>> pages_;
    std::size_t encoded_bytes_{};
    static std::shared_ptr<const Metadata> admit_metadata(nlohmann::json);
    void replace_page(unsigned key, std::shared_ptr<const Page>);
};
struct PaintTextureSource {
    PaintTextureValue document;
    static PaintTextureSource create(AssetId id) { return {PaintTextureValue::create(id)}; }
    static PaintTextureSource parse(std::span<const std::byte>);
    AssetId asset() const { return document.asset(); }
    void validate() const { document.validate(); }
};
} // namespace forge
