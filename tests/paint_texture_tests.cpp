#include "color_transfer.hpp"
#include "paint_texture_source.hpp"
#include "paint_texture_task.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid paint input was admitted");
}
PaintTextureValue roundtrip(const PaintTextureValue& source) {
    const auto text = source.dump(2);
    return PaintTextureSource::parse(std::as_bytes(std::span(text))).document;
}
PaintPixel oracle(PaintPixel pixel, PaintPixel background, TextureSemantic semantic,
                  const PaintBrush& brush, double coverage) {
    const auto quantize = [](double x) {
        return std::uint8_t(std::clamp(std::lround(x * 255), 0l, 255l));
    };
    const double amount = coverage * brush.opacity;
    if (semantic == TextureSemantic::Data) {
        for (unsigned c = 0; c < 4; ++c)
            pixel[c] = quantize(pixel[c] / 255. * (1 - amount) +
                                amount * (brush.erase ? background[c] / 255. : brush.color[c]));
        return pixel;
    }
    if (brush.erase) {
        pixel[3] = quantize(pixel[3] / 255. * (1 - amount));
        return pixel;
    }
    const double a = brush.color[3] * amount, old_alpha = pixel[3] / 255.;
    if (a == 0)
        return pixel;
    const double alpha = a + old_alpha * (1 - a);
    for (unsigned c = 0; c < 3; ++c) {
        const double sample = pixel[c] / 255.;
        const double decoded =
            sample <= .04045 ? sample / 12.92 : std::pow((sample + .055) / 1.055, 2.4);
        const double color = (brush.color[c] * a + decoded * old_alpha * (1 - a)) / alpha;
        const double encoded =
            color <= .0031308 ? color * 12.92 : 1.055 * std::pow(color, 1. / 2.4) - .055;
        pixel[c] = quantize(encoded);
    }
    pixel[3] = quantize(alpha);
    return pixel;
}
// Dense all-pixel oracle computes one nearest path distance before its blend;
// it does not traverse producer rectangles, pages or coverage accumulation.
double coverage_at(unsigned x, unsigned y, std::span<const PaintPoint> points,
                   const PaintBrush& brush) {
    double distance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < std::max(std::size_t(1), points.size() - 1); ++i) {
        const auto a = points[i], b = points[std::min(i + 1, points.size() - 1)];
        const double vx = b.x - a.x, vy = b.y - a.y;
        const double length = vx * vx + vy * vy;
        const double px = x + .5, py = y + .5;
        const double t =
            length == 0 ? 0 : std::clamp(((px - a.x) * vx + (py - a.y) * vy) / length, 0., 1.);
        const double dx = px - (a.x + t * vx), dy = py - (a.y + t * vy);
        distance = std::min(distance, std::sqrt(dx * dx + dy * dy));
    }
    return float(std::clamp((brush.radius + .5 - distance) /
                                std::max(1., brush.radius * (1 - brush.hardness)),
                            0., 1.));
}
void benchmark() {
    auto source = PaintTextureValue::create(AssetId::generate(), 1024, 1024);
    const std::array<PaintPoint, 5> line{
        {{20.5, 20.5}, {250.5, 850.5}, {500.5, 100.5}, {750.5, 850.5}, {1000.5, 20.5}}};
    PaintBrush brush;
    brush.radius = 32;
    const auto start = std::chrono::steady_clock::now();
    const auto painted = source.paint(line, brush);
    const auto painted_at = std::chrono::steady_clock::now();
    const auto texture = painted.texture();
    const auto exported_at = std::chrono::steady_clock::now();
    const auto text = painted.dump();
    const auto saved_at = std::chrono::steady_clock::now();
    std::cout
        << nlohmann::
               json{{"width", 1024},
                    {"height", 1024},
                    {"path_points", line.size()},
                    {"radius", brush.radius},
                    {"pages", painted.page_count()},
                    {"paint_ms",
                     std::chrono::duration<double, std::milli>(painted_at - start).count()},
                    {"rgba_export_ms",
                     std::chrono::duration<double, std::milli>(exported_at - painted_at).count()},
                    {"serialize_ms",
                     std::chrono::duration<double, std::milli>(saved_at - exported_at).count()},
                    {"source_bytes", text.size()},
                    {"pixels_bytes", texture.byte_size()},
                    {"history_weight", PaintTextureValue::history_weight(source, painted)},
                    {"scope",
                     "Local CPU source operations; not GUI latency or physical GPU acceptance"}}
                   .dump()
        << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
            benchmark();
            return 0;
        }
        check(argc == 1, "Expected optional --benchmark");
        const auto id = AssetId::generate();
        auto color = PaintTextureValue::create(id, 67, 35);
        check(color.asset() == id && color.page_count() == 0 && color.pixel(66, 34) == PaintPixel{},
              "Blank sparse paint source differs");
        check(roundtrip(color) == color, "Blank paint source did not round-trip");
        PaintBrush brush;
        brush.radius = .5;
        brush.hardness = 1;
        brush.color = {.5, .5, .5, 1};
        const std::array<PaintPoint, 3> islands{{{2.5, 2.5}, {20.5, 2.5, true}, {25.5, 2.5}}};
        const auto disconnected = color.paint(islands, brush);
        check(disconnected.pixel(2, 2) == PaintPixel{188, 188, 188, 255} &&
                  disconnected.pixel(10, 2) == PaintPixel{} &&
                  disconnected.pixel(22, 2) == PaintPixel{188, 188, 188, 255},
              "Disconnected UV paths bridged islands or omitted their end points");
        auto translucent = brush;
        translucent.opacity = .5;
        const std::array<PaintPoint, 2> disconnected_retrace{{{2.5, 2.5}, {2.5, 2.5, true}}};
        check(color.paint(disconnected_retrace, translucent).pixel(2, 2) ==
                  PaintPixel{188, 188, 188, 128},
              "Disconnected paths accumulated opacity within one stroke");
        const std::array<PaintPoint, 1> point{{{66.5, 34.5}}};
        const auto painted = color.paint(point, brush);
        check(painted.pixel(66, 34) == PaintPixel{188, 188, 188, 255} &&
                  painted.page_count() == 1 && color.page_count() == 0 &&
                  color.pixel(66, 34) == PaintPixel{} && painted.pixel(65, 34) == PaintPixel{} &&
                  roundtrip(painted) == painted,
              "Color painting lost linear/sRGB interpretation, clipping or snapshot isolation");
        auto data = PaintTextureValue::create(id, 67, 35, TextureSemantic::Data);
        check(data.paint(point, brush).pixel(66, 34) == PaintPixel{128, 128, 128, 255},
              "Data painting applied RGB transfer");
        Rgba8Pixels copy_pixels{67, 35, std::vector<std::uint8_t>(67 * 35 * 4, 128), true};
        const auto exact_copy = color.replace_pixels(copy_pixels);
        check(exact_copy.pixel(0, 0) == PaintPixel{128, 128, 128, 128} &&
                  exact_copy.asset() == id &&
                  exact_copy.pixel(66, 34) == PaintPixel{128, 128, 128, 128} &&
                  roundtrip(exact_copy) == exact_copy,
              "Stored sRGB copy changed samples, identity or odd padding");
        copy_pixels.srgb = false;
        check(color.replace_pixels(copy_pixels).pixel(0, 0) == PaintPixel{188, 188, 188, 128},
              "Linear Color copy failed to encode RGB or changed alpha");
        check(data.replace_pixels(copy_pixels).pixel(0, 0) == PaintPixel{128, 128, 128, 128},
              "Data copy applied color transfer");
        auto wrong_pixels = copy_pixels;
        wrong_pixels.width = 68;
        rejects([&] { color.replace_pixels(wrong_pixels); });
        wrong_pixels = copy_pixels;
        wrong_pixels.rgba.pop_back();
        rejects([&] { color.replace_pixels(wrong_pixels); });
        wrong_pixels = copy_pixels;
        wrong_pixels.srgb = true;
        rejects([&] { data.replace_pixels(wrong_pixels); });
        brush.color = {1, 0, 0, .5};
        check(color.paint(point, brush).pixel(66, 34) == PaintPixel{255, 0, 0, 128},
              "Straight-alpha paint became premultiplied storage");
        auto partial = color.fill({187, 21, 52, 128});
        brush.erase = true;
        brush.opacity = .5;
        check(partial.paint(point, brush).pixel(66, 34) == PaintPixel{187, 21, 52, 64},
              "Color erasing changed hidden RGB or used the brush alpha as eraser strength");
        brush.erase = false;
        brush.opacity = 0;
        check(painted.paint(point, brush) == painted, "Zero-opacity paint changed source");
        brush.opacity = 1;
        const std::array<PaintPoint, 1> outside{{{-500, -500}}};
        check(painted.paint(outside, brush) == painted, "Outside paint changed the canvas");
        brush = {};
        brush.radius = 2.5;
        brush.hardness = 1;
        brush.opacity = .5;
        const std::array<PaintPoint, 2> straight{{{.5, 10.5}, {40.5, 10.5}}};
        const std::array<PaintPoint, 3> retraced{{straight[0], straight[1], straight[0]}};
        const auto once = color.paint(straight, brush);
        check(color.paint(retraced, brush) == once,
              "Retracing one stroke accumulated opacity repeatedly");
        check(once.paint(straight, brush).pixel(20, 10)[3] > once.pixel(20, 10)[3],
              "Separate strokes could not layer opacity");
        std::vector<PaintPoint> subdivided;
        for (unsigned i = 0; i <= 40; ++i)
            subdivided.push_back({i + .5, 10.5});
        check(color.paint(subdivided, brush) == once,
              "Path sampling density changed continuous stroke coverage");
        std::mt19937 random(77145);
        std::uniform_real_distribution<double> unit(0, 1), location(-15, 80);
        for (unsigned trial = 0; trial < 80; ++trial) {
            const auto semantic = trial % 2 ? TextureSemantic::Color : TextureSemantic::Data;
            const PaintPixel background{31, 73, 159, 117};
            const auto before = PaintTextureValue::create(id, 67, 35, semantic, background);
            std::array<PaintPoint, 4> path;
            for (auto& p : path)
                p = {location(random), location(random)};
            brush.radius = .5 + unit(random) * 12;
            brush.hardness = unit(random);
            brush.opacity = unit(random);
            brush.erase = trial % 3 == 0;
            for (auto& c : brush.color)
                c = unit(random);
            const auto result = before.paint(path, brush);
            const auto image = result.texture();
            for (unsigned y = 0; y < before.height(); ++y)
                for (unsigned x = 0; x < before.width(); ++x) {
                    const auto expected = oracle(background, background, semantic, brush,
                                                 coverage_at(x, y, path, brush));
                    const auto actual = result.pixel(x, y);
                    for (unsigned c = 0; c < 4; ++c)
                        check(std::to_integer<unsigned>(
                                  image.subresources[0][(std::size_t(y) * before.width() + x) * 4 +
                                                        c]) == actual[c],
                              "Bulk image export differs from page pixels or their row order");
                    for (unsigned c = 0; c < 4; ++c)
                        check(std::abs(int(expected[c]) - actual[c]) <= 1,
                              "Paged paint differs from independent dense path/blend oracle");
                }
            check(before.page_count() == 0 && roundtrip(result) == result,
                  "Path painting mutated baseline or lost serialized bytes");
        }
        auto json = nlohmann::json::parse(painted.dump());
        json["future"] = {{"keep", true}};
        json["tiles"][0]["extension"] = "retain";
        const auto extended = PaintTextureValue::parse(json);
        const auto filled = extended.fill({10, 20, 30, 40});
        const auto saved = nlohmann::json::parse(filled.dump());
        check(saved["future"] == json["future"] && saved["tiles"][0]["extension"] == "retain" &&
                  roundtrip(filled) == filled,
              "Fill lost opaque source extensions");
        check(painted.texture().subresources == roundtrip(painted).texture().subresources &&
                  painted.texture().format == TextureFormat::RGBA8Srgb &&
                  data.texture().alpha == TextureAlpha::Custom,
              "Paint output is not an ordinary complete TextureData value");
        for (const auto field : {"width", "height", "version"}) {
            auto bad = json;
            bad[field] = true;
            rejects([&] { PaintTextureValue::parse(bad); });
        }
        for (const auto dimensions : {0u, 1025u})
            rejects([&] { PaintTextureValue::create(id, dimensions, 1); });
        rejects([&] { PaintTextureValue::create(id, 1, 1, TextureSemantic::Normal); });
        auto bad = json;
        bad["tiles"][0]["rgba"] = "ff";
        rejects([&] { PaintTextureValue::parse(bad); });
        bad = json;
        bad["tiles"][0]["rgba"].get_ref<std::string&>()[0] = 'x';
        rejects([&] { PaintTextureValue::parse(bad); });
        bad = json;
        bad["tiles"].push_back(bad["tiles"][0]);
        rejects([&] { PaintTextureValue::parse(bad); });
        bad = json;
        bad["tiles"][0]["rgba"].get_ref<std::string&>().replace(8184, 2, "ff");
        rejects([&] { PaintTextureValue::parse(bad); }); // Outside the odd-sized canvas.
        brush = {};
        brush.radius = .49;
        rejects([&] { color.paint(point, brush); });
        brush.radius = 257;
        rejects([&] { color.paint(point, brush); });
        brush = {};
        brush.opacity = NAN;
        rejects([&] { color.paint(point, brush); });
        brush = {};
        rejects([&] { color.paint({}, brush); });
        const std::vector<PaintPoint> too_many(paint_texture_stroke_point_limit + 1);
        rejects([&] { color.paint(too_many, brush); });
        rejects([&] { color.pixel(67, 0); });
        std::stop_source cancelled;
        cancelled.request_stop();
        rejects([&] { color.paint(point, brush, cancelled.get_token()); });
        PaintTextureTask task;
        task.start(color, {point.begin(), point.end()}, brush, 7, 12);
        rejects([&] { task.start(color, {point.begin(), point.end()}, brush, 7, 12); });
        std::optional<PaintTextureTask::Result> prepared;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!(prepared = task.poll()) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(prepared && prepared->error.empty() && prepared->session == 7 &&
                  prepared->revision == 12 && prepared->value == color.paint(point, brush) &&
                  !task.pending(),
              "Detached stroke preparation lost source, revision or pixels");
        task.start(color, {point.begin(), point.end()}, brush, 7, 12);
        task.cancel();
        check(!task.pending() && !task.poll(), "Cancelled stroke retained a prepared result");
        for (unsigned value = 0; value < 256; ++value)
            check(std::lround(linear_to_srgb(srgb_to_linear(value / 255.)) * 255) == value,
                  "Shared RGB transfer changed a byte round-trip");
        std::cout
            << "Sparse paint source, independent dense paths/color/alpha, clipping, immutable "
               "snapshots, serialization/extensions and invalid admission passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
