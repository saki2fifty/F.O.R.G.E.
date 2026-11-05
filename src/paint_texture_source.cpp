#include "paint_texture_source.hpp"
#include "bounded_json.hpp"
#include "color_transfer.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr unsigned side = paint_texture_page_side, page_pixels = side * side;
constexpr std::uint64_t stroke_work_limit = 64ull * 1024 * 1024;
void need(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
unsigned natural(const Json& value, unsigned maximum) {
    need(value.is_number_unsigned() ||
             (value.is_number_integer() && value.get<std::int64_t>() >= 0),
         "Paint texture values must be nonnegative integers");
    const auto integer = value.get<std::uint64_t>();
    need(integer <= maximum, "Paint texture integer exceeds its limit");
    return unsigned(integer);
}
PaintPixel read_pixel(const Json& value) {
    need(value.is_array() && value.size() == 4, "Paint texture needs four byte channels");
    PaintPixel result;
    for (unsigned i = 0; i < 4; ++i)
        result[i] = std::uint8_t(natural(value[i], 255));
    return result;
}
std::uint8_t byte(double value) {
    return std::uint8_t(std::clamp(std::lround(value * 255), 0l, 255l));
}
PaintPixel blend(PaintPixel before, PaintPixel background, TextureSemantic semantic,
                 const PaintBrush& brush, double coverage) {
    const double opacity = coverage * brush.opacity;
    if (!(opacity > 0))
        return before;
    auto result = before;
    if (semantic == TextureSemantic::Data) {
        for (unsigned channel = 0; channel < 4; ++channel) {
            const double target = brush.erase ? background[channel] / 255. : brush.color[channel];
            result[channel] = byte(before[channel] / 255. * (1 - opacity) + target * opacity);
        }
    } else if (brush.erase) {
        result[3] = byte(before[3] / 255. * (1 - opacity));
    } else {
        const double source_alpha = opacity * brush.color[3], target_alpha = before[3] / 255.;
        if (!(source_alpha > 0))
            return before;
        const double alpha = source_alpha + target_alpha * (1 - source_alpha);
        for (unsigned channel = 0; channel < 3; ++channel) {
            const double previous = srgb_to_linear(before[channel] / 255.);
            const double linear = (brush.color[channel] * source_alpha +
                                   previous * target_alpha * (1 - source_alpha)) /
                                  alpha;
            result[channel] = byte(linear_to_srgb(linear));
        }
        result[3] = byte(alpha);
    }
    return result;
}
struct Rectangle {
    unsigned left{}, top{}, right{}, bottom{};
};
Rectangle footprint(PaintPoint a, PaintPoint b, double radius, unsigned width, unsigned height) {
    const auto edge = [](double value, unsigned maximum, bool upper) {
        return unsigned(
            std::clamp(upper ? std::ceil(value) : std::floor(value), 0., double(maximum)));
    };
    return {edge(std::min(a.x, b.x) - radius - .5, width, false),
            edge(std::min(a.y, b.y) - radius - .5, height, false),
            edge(std::max(a.x, b.x) + radius + .5, width, true),
            edge(std::max(a.y, b.y) + radius + .5, height, true)};
}
} // namespace
void validate_paint_brush(const PaintBrush& value) {
    need(std::isfinite(value.radius) && value.radius >= .5 && value.radius <= 256 &&
             std::isfinite(value.hardness) && value.hardness >= 0 && value.hardness <= 1 &&
             std::isfinite(value.opacity) && value.opacity >= 0 && value.opacity <= 1,
         "Paint brush requires radius 0.5 to 256 and hardness/opacity 0 to 1");
    for (const auto channel : value.color)
        need(std::isfinite(channel) && channel >= 0 && channel <= 1,
             "Paint brush channels must be finite from 0 to 1");
}
std::shared_ptr<const PaintTextureValue::Metadata> PaintTextureValue::admit_metadata(Json root) {
    need(root.is_object() && root.at("format") == "forge.paint-texture" &&
             natural(root.at("version"), 1) == 1 && !root.contains("tiles"),
         "Unsupported paint texture metadata");
    auto result = std::make_shared<Metadata>();
    result->asset = root.at("asset_id").get<AssetId>();
    need(bool(result->asset), "Paint texture needs an asset UUID");
    result->width = natural(root.at("width"), paint_texture_dimension_limit);
    result->height = natural(root.at("height"), paint_texture_dimension_limit);
    need(result->width && result->height, "Paint texture dimensions must be nonempty");
    const auto semantic = root.at("semantic").get<std::string>();
    need(semantic == "color" || semantic == "data", "Paint texture supports color or data");
    result->semantic = semantic == "color" ? TextureSemantic::Color : TextureSemantic::Data;
    result->background = read_pixel(root.at("background"));
    result->encoded_bytes = root.dump().size();
    need(result->encoded_bytes <= paint_texture_source_byte_limit - 64,
         "Paint texture metadata exceeds its source budget");
    result->document = std::move(root);
    return result;
}
PaintTextureValue PaintTextureValue::create(AssetId asset, unsigned width, unsigned height,
                                            TextureSemantic semantic, PaintPixel background) {
    need(semantic == TextureSemantic::Color || semantic == TextureSemantic::Data,
         "Paint texture supports color or data");
    PaintTextureValue result;
    result.metadata_ =
        admit_metadata({{"format", "forge.paint-texture"},
                        {"version", 1},
                        {"asset_id", asset},
                        {"width", width},
                        {"height", height},
                        {"semantic", semantic == TextureSemantic::Color ? "color" : "data"},
                        {"background", background}});
    return result;
}
PaintTextureValue PaintTextureValue::parse(Json root) {
    need(root.is_object() && root.contains("tiles") && root.at("tiles").is_array(),
         "Paint texture source needs a tile array");
    // Native DOM callers receive the same byte/depth/event admission as files.
    const auto encoded = root.dump();
    (void)asset_detail::parse_bounded_json(std::as_bytes(std::span(encoded)),
                                           paint_texture_source_byte_limit, 100000, 16);
    auto tiles = std::move(root["tiles"]);
    root.erase("tiles");
    PaintTextureValue result;
    result.metadata_ = admit_metadata(std::move(root));
    const unsigned columns = (result.width() + side - 1) / side,
                   rows = (result.height() + side - 1) / side;
    need(tiles.size() <= std::size_t(columns) * rows, "Paint texture has too many tiles");
    for (auto& tile : tiles) {
        need(tile.is_object(), "Paint texture tile must be an object");
        const unsigned x = natural(tile.at("x"), columns - 1), y = natural(tile.at("y"), rows - 1);
        const unsigned key = y * columns + x;
        need(!result.pages_.contains(key), "Paint texture tile coordinate is duplicated");
        const auto& text = tile.at("rgba").get_ref<const std::string&>();
        need(text.size() == page_pixels * 8, "Paint texture tile must encode 32x32 RGBA bytes");
        auto page = std::make_shared<Page>();
        for (unsigned pixel = 0; pixel < page_pixels; ++pixel)
            for (unsigned channel = 0; channel < 4; ++channel) {
                const auto* first = text.data() + pixel * 8 + channel * 2;
                unsigned value = 0;
                const auto parsed = std::from_chars(first, first + 2, value, 16);
                need(parsed.ec == std::errc{} && parsed.ptr == first + 2,
                     "Paint texture tile contains an invalid hexadecimal byte");
                page->pixels[pixel][channel] = std::uint8_t(value);
            }
        for (unsigned py = 0; py < side; ++py)
            for (unsigned px = 0; px < side; ++px)
                if (x * side + px >= result.width() || y * side + py >= result.height())
                    need(page->pixels[py * side + px] == result.background(),
                         "Paint texture tile padding must retain the background pixel");
        tile.erase("x");
        tile.erase("y");
        tile.erase("rgba");
        page->extensions = std::move(tile);
        page->encoded_bytes = page_pixels * 8 + page->extensions.dump().size() + 128;
        result.replace_page(key, std::move(page));
    }
    result.validate();
    return result;
}
void PaintTextureValue::validate() const {
    need(bool(metadata_) && metadata_->width && metadata_->height &&
             pages_.size() <= std::size_t((metadata_->width + side - 1) / side) *
                                  ((metadata_->height + side - 1) / side) &&
             metadata_->encoded_bytes + encoded_bytes_ + 64 <= paint_texture_source_byte_limit,
         "Paint texture value is absent or exceeds its budget");
}
AssetId PaintTextureValue::asset() const {
    validate();
    return metadata_->asset;
}
unsigned PaintTextureValue::width() const {
    validate();
    return metadata_->width;
}
unsigned PaintTextureValue::height() const {
    validate();
    return metadata_->height;
}
TextureSemantic PaintTextureValue::semantic() const {
    validate();
    return metadata_->semantic;
}
PaintPixel PaintTextureValue::background() const {
    validate();
    return metadata_->background;
}
const Json& PaintTextureValue::metadata() const {
    validate();
    return metadata_->document;
}
PaintPixel PaintTextureValue::pixel(unsigned x, unsigned y) const {
    validate();
    need(x < width() && y < height(), "Paint texture pixel is outside the canvas");
    const unsigned key = (y / side) * ((width() + side - 1) / side) + x / side;
    const auto found = pages_.find(key);
    return found == pages_.end() ? background()
                                 : found->second->pixels[(y % side) * side + x % side];
}
void PaintTextureValue::replace_page(unsigned key, std::shared_ptr<const Page> page) {
    if (const auto old = pages_.find(key); old != pages_.end()) {
        encoded_bytes_ -= old->second->encoded_bytes;
        pages_.erase(old);
    }
    if (page) {
        encoded_bytes_ += page->encoded_bytes;
        pages_.emplace(key, std::move(page));
    }
}
PaintTextureValue PaintTextureValue::with_metadata(Json root) const {
    validate();
    auto next = admit_metadata(std::move(root));
    need(next->width == width() && next->height == height() && next->semantic == semantic() &&
             next->background == background(),
         "Paint texture metadata cannot replace its canvas, background or pixel interpretation");
    auto result = *this;
    result.metadata_ = std::move(next);
    result.validate();
    return result;
}
PaintTextureValue PaintTextureValue::replace_pixels(const Rgba8Pixels& pixels,
                                                    std::stop_token stop) const {
    validate();
    need(!stop.stop_requested(), "Pixel copy cancelled");
    need(pixels.width == width() && pixels.height == height() &&
             pixels.rgba.size() == std::size_t(width()) * height() * 4,
         "Pixel copy must match the paint canvas dimensions");
    need(semantic() == TextureSemantic::Color || !pixels.srgb,
         "Data pixels cannot carry an sRGB interpretation");
    auto result = *this;
    result.pages_.clear();
    result.encoded_bytes_ = 0;
    const auto canvas_width = width(), canvas_height = height();
    const auto background_pixel = background();
    const bool encode_color = semantic() == TextureSemantic::Color && !pixels.srgb;
    const unsigned columns = (canvas_width + side - 1) / side;
    for (unsigned py = 0; py < (canvas_height + side - 1) / side; ++py)
        for (unsigned px = 0; px < columns; ++px) {
            need(!stop.stop_requested(), "Pixel copy cancelled");
            const auto key = py * columns + px;
            auto page = std::make_shared<Page>();
            page->pixels.fill(background_pixel);
            if (const auto old = pages_.find(key); old != pages_.end())
                page->extensions = old->second->extensions;
            for (unsigned y = 0; y < std::min(side, canvas_height - py * side); ++y)
                for (unsigned x = 0; x < std::min(side, canvas_width - px * side); ++x) {
                    auto& pixel = page->pixels[y * side + x];
                    std::memcpy(pixel.data(),
                                pixels.rgba.data() +
                                    ((std::size_t(py * side + y) * canvas_width) + px * side + x) *
                                        4,
                                4);
                    if (encode_color)
                        for (unsigned channel = 0; channel < 3; ++channel)
                            pixel[channel] = byte(linear_to_srgb(pixel[channel] / 255.));
                }
            if (!page->extensions.empty() ||
                !std::all_of(page->pixels.begin(), page->pixels.end(),
                             [&](auto p) { return p == background_pixel; })) {
                page->encoded_bytes = page_pixels * 8 + page->extensions.dump().size() + 128;
                result.replace_page(key, std::move(page));
            }
        }
    result.validate();
    return result;
}
PaintTextureValue PaintTextureValue::fill(PaintPixel value) const {
    validate();
    auto result = *this;
    auto root = metadata();
    root["background"] = value;
    result.metadata_ = admit_metadata(std::move(root));
    result.pages_.clear();
    result.encoded_bytes_ = 0;
    for (const auto& [key, old] : pages_)
        if (!old->extensions.empty()) {
            auto page = std::make_shared<Page>(*old);
            page->pixels.fill(value);
            result.replace_page(key, std::move(page));
        }
    result.validate();
    return result;
}
PaintTextureValue PaintTextureValue::paint(std::span<const PaintPoint> points,
                                           const PaintBrush& brush, std::stop_token stop) const {
    need(!stop.stop_requested(), "Paint stroke cancelled");
    validate();
    const unsigned canvas_width = metadata_->width, canvas_height = metadata_->height;
    const auto background_pixel = metadata_->background;
    const auto image_semantic = metadata_->semantic;
    validate_paint_brush(brush);
    need(!points.empty() && points.size() <= paint_texture_stroke_point_limit,
         "Paint stroke needs 1 to 256 points");
    for (const auto& p : points)
        need(std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1e6 &&
                 std::abs(p.y) <= 1e6,
             "Paint stroke coordinates must be finite within +/-1000000 pixels");
    std::vector<Rectangle> rectangles;
    const auto segments = std::max(std::size_t(1), points.size() - 1);
    std::uint64_t work = 0;
    for (std::size_t i = 0; i < segments; ++i) {
        const auto b = points[std::min(i + 1, points.size() - 1)];
        const auto rectangle = footprint(points[i], b, brush.radius, canvas_width, canvas_height);
        work +=
            std::uint64_t(rectangle.right - rectangle.left) * (rectangle.bottom - rectangle.top);
        need(work <= stroke_work_limit, "Paint stroke is too large; use a shorter stroke");
        rectangles.push_back(rectangle);
    }
    if (!work || brush.opacity == 0)
        return *this;
    std::vector<float> coverage(std::size_t(canvas_width) * canvas_height, 0);
    const double edge = std::max(1., brush.radius * (1 - brush.hardness));
    for (std::size_t i = 0; i < segments; ++i) {
        const auto a = points[i], b = points[std::min(i + 1, points.size() - 1)];
        const double dx = b.x - a.x, dy = b.y - a.y, length_squared = dx * dx + dy * dy;
        const auto& r = rectangles[i];
        for (unsigned y = r.top; y < r.bottom; ++y) {
            need(!stop.stop_requested(), "Paint stroke cancelled");
            for (unsigned x = r.left; x < r.right; ++x) {
                const double px = x + .5 - a.x, py = y + .5 - a.y;
                const double t = length_squared > 0
                                     ? std::clamp((px * dx + py * dy) / length_squared, 0., 1.)
                                     : 0.;
                const double rx = px - t * dx, ry = py - t * dy;
                const double squared_distance = rx * rx + ry * ry;
                const double outer = brush.radius + .5;
                // Coordinates are admitted within +/-1e6, so this squared norm
                // cannot overflow. Reject outside coverage before taking a root.
                if (squared_distance >= outer * outer)
                    continue;
                const double distance = std::sqrt(squared_distance);
                const auto weight =
                    float(std::clamp((brush.radius + .5 - distance) / edge, 0., 1.));
                auto& prior = coverage[std::size_t(y) * canvas_width + x];
                prior = std::max(prior, weight);
            }
        }
    }
    auto result = *this;
    const unsigned columns = (canvas_width + side - 1) / side;
    for (unsigned py = 0; py < (canvas_height + side - 1) / side; ++py)
        for (unsigned px = 0; px < columns; ++px) {
            need(!stop.stop_requested(), "Paint stroke cancelled");
            const unsigned key = py * columns + px;
            const auto prior = pages_.find(key);
            std::shared_ptr<Page> changed;
            for (unsigned y = 0; y < std::min(side, canvas_height - py * side); ++y)
                for (unsigned x = 0; x < std::min(side, canvas_width - px * side); ++x) {
                    const auto weight =
                        coverage[std::size_t(py * side + y) * canvas_width + px * side + x];
                    if (!(weight > 0))
                        continue;
                    const auto before = prior == pages_.end() ? background_pixel
                                                              : prior->second->pixels[y * side + x];
                    const auto after =
                        blend(before, background_pixel, image_semantic, brush, weight);
                    if (before == after)
                        continue;
                    if (!changed) {
                        if (prior != pages_.end())
                            changed = std::make_shared<Page>(*prior->second);
                        else {
                            changed = std::make_shared<Page>();
                            changed->pixels.fill(background_pixel);
                            changed->encoded_bytes = page_pixels * 8 + 130;
                        }
                    }
                    changed->pixels[y * side + x] = after;
                }
            if (changed) {
                const bool uniform =
                    std::all_of(changed->pixels.begin(), changed->pixels.end(),
                                [&](PaintPixel p) { return p == background_pixel; });
                if (uniform && changed->extensions.empty())
                    result.replace_page(key, {});
                else
                    result.replace_page(key, std::move(changed));
            }
        }
    result.validate();
    return result;
}
TextureData PaintTextureValue::texture() const {
    validate();
    TextureData result;
    result.width = metadata_->width;
    result.height = metadata_->height;
    result.semantic = metadata_->semantic;
    result.format =
        result.semantic == TextureSemantic::Color ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8;
    result.alpha =
        result.semantic == TextureSemantic::Color ? TextureAlpha::Straight : TextureAlpha::Custom;
    result.subresources.emplace_back(std::size_t(result.width) * result.height * 4);
    auto& output = result.subresources.front();
    const auto background = metadata_->background;
    static_assert(sizeof(PaintPixel) == 4);
    if (std::all_of(background.begin(), background.end(),
                    [&](auto c) { return c == background[0]; }))
        std::fill(output.begin(), output.end(), std::byte(background[0]));
    else
        for (std::size_t at = 0; at < output.size(); at += 4)
            std::memcpy(output.data() + at, background.data(), 4);
    const unsigned columns = (result.width + side - 1) / side;
    for (const auto& [key, page] : pages_) {
        const unsigned x = (key % columns) * side, y = (key / columns) * side;
        const unsigned row_bytes = std::min(side, result.width - x) * 4;
        for (unsigned row = 0; row < std::min(side, result.height - y); ++row)
            std::memcpy(output.data() + ((std::size_t(y) + row) * result.width + x) * 4,
                        page->pixels.data() + row * side, row_bytes);
    }
    validate_texture(result);
    return result;
}
std::string PaintTextureValue::dump(int indent) const {
    validate();
    auto root = metadata();
    root["tiles"] = Json::array();
    constexpr char digits[] = "0123456789abcdef";
    for (const auto& [key, page] : pages_) {
        auto tile = page->extensions;
        tile["x"] = key % ((width() + side - 1) / side);
        tile["y"] = key / ((width() + side - 1) / side);
        std::string encoded(page_pixels * 8, '0');
        for (unsigned p = 0; p < page_pixels; ++p)
            for (unsigned c = 0; c < 4; ++c) {
                const auto value = page->pixels[p][c];
                encoded[p * 8 + c * 2] = digits[value >> 4];
                encoded[p * 8 + c * 2 + 1] = digits[value & 15];
            }
        tile["rgba"] = std::move(encoded);
        root["tiles"].push_back(std::move(tile));
    }
    return root.dump(indent);
}
std::size_t PaintTextureValue::history_weight(const PaintTextureValue& from,
                                              const PaintTextureValue& to) {
    from.validate();
    to.validate();
    // Retained snapshot nodes plus old pages no longer shared with the new value.
    // Current/saved snapshots remain separate from the document's history budget.
    std::size_t bytes =
        from.metadata_->encoded_bytes +
        from.pages_.size() *
            (sizeof(std::pair<const unsigned, std::shared_ptr<const Page>>) + sizeof(void*) * 4);
    for (const auto& [key, page] : from.pages_) {
        const auto next = to.pages_.find(key);
        if (next == to.pages_.end() || next->second != page)
            bytes += sizeof(Page) + page->extensions.dump().size();
    }
    return bytes;
}
bool operator==(const PaintTextureValue& a, const PaintTextureValue& b) {
    if (!a.metadata_ || !b.metadata_)
        return a.metadata_ == b.metadata_;
    if (a.pages_.size() != b.pages_.size() ||
        (a.metadata_ != b.metadata_ && a.metadata_->document != b.metadata_->document))
        return false;
    auto left = a.pages_.begin(), right = b.pages_.begin();
    for (; left != a.pages_.end(); ++left, ++right)
        if (left->first != right->first ||
            (left->second != right->second &&
             (left->second->pixels != right->second->pixels ||
              left->second->extensions != right->second->extensions)))
            return false;
    return true;
}
PaintTextureSource PaintTextureSource::parse(std::span<const std::byte> bytes) {
    return {PaintTextureValue::parse(
        asset_detail::parse_bounded_json(bytes, paint_texture_source_byte_limit, 100000, 16))};
}
} // namespace forge
