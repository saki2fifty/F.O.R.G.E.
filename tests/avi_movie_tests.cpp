#include "avi_movie.hpp"
#include "texture_decode.h"
#include "texture_encode.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <forge/identity.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

using namespace forge::asset_detail;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid movie input was accepted");
}
std::uint32_t integer(std::span<const std::byte> file, std::size_t at) {
    check(at <= file.size() && file.size() - at >= 4, "Truncated movie integer");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(std::to_integer<unsigned char>(file[at + i])) << (8 * i);
    return value;
}
bool tag(std::span<const std::byte> file, std::size_t at, const char* text) {
    if (at > file.size() || file.size() - at < 4)
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (file[at + i] != std::byte(text[i]))
            return false;
    return true;
}
std::span<const std::byte> chunk(std::span<const std::byte> list, const char* name) {
    for (std::size_t at = 0; at < list.size();) {
        const auto length = integer(list, at + 4);
        check(list.size() - at >= 8 && length <= list.size() - at - 8,
              "Movie chunk extends beyond its owner");
        if (tag(list, at, name))
            return list.subspan(at + 8, length);
        at += 8 + length + (length & 1);
    }
    throw std::runtime_error("Missing movie chunk");
}
std::span<const std::byte> list(std::span<const std::byte> data, const char* kind) {
    for (std::size_t at = 0; at < data.size();) {
        const auto length = integer(data, at + 4);
        check(data.size() - at >= 8 && length <= data.size() - at - 8,
              "Movie list extends beyond its owner");
        if (tag(data, at, "LIST") && length >= 4 && tag(data, at + 8, kind))
            return data.subspan(at + 12, length - 4);
        at += 8 + length + (length & 1);
    }
    throw std::runtime_error("Missing movie list");
}
void save(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
void timing() {
    check(avi_movie_frame_rate(24) == MovieFrameRate{24, 1}, "Integer movie FPS changed");
    check(avi_movie_frame_rate(29.97) == MovieFrameRate{2997, 100}, "Decimal FPS lost precision");
    check(avi_movie_frame_rate(30000. / 1001) == MovieFrameRate{30000, 1001},
          "Fractional movie FPS accumulated timing drift");
    check(avi_movie_frame_rate(.001) == MovieFrameRate{1, 1000}, "Minimum movie FPS rejected");
    for (double fps : {.0011, .125, 1. / 3, 23.976, 1.234567891, 239.99999999, 240.}) {
        const auto value = avi_movie_frame_rate(fps);
        check(std::abs(double(value.rate) / value.scale - fps) <= std::max(1e-10, fps * 1e-10),
              "Movie rational exceeded its promised precision");
    }
    for (double fps : {0., -.1, .0009, 241., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { (void)avi_movie_frame_rate(fps); });
    for (const auto rate :
         {MovieFrameRate{}, MovieFrameRate{30, 0}, MovieFrameRate{0, 1}, MovieFrameRate{60, 2},
          MovieFrameRate{241, 1}, MovieFrameRate{1, 1000001}})
        rejects([&] { AviMovie movie(4, 4, rate, 1); });
}
void boundaries() {
    const auto rate = avi_movie_frame_rate(30);
    for (const auto extent : {0u, 4097u})
        rejects([&] { AviMovie movie(extent, 4, rate, 1); });
    for (const auto count : {0u, 4097u})
        rejects([&] { AviMovie movie(4, 4, rate, count); });
    rejects([&] { AviMovie movie(4, 4, rate, 1, 10); });
    rejects([&] { AviMovie movie(4, 4, rate, 1, avi_movie_byte_limit + 1); });
    AviMovie bounded(4, 4, rate, 1, 300);
    rejects([&] { (void)bounded.finish(); });
    const std::array<std::byte, 5> packet{std::byte{255}, std::byte{216}, std::byte{0},
                                          std::byte{255}, std::byte{217}};
    rejects([&] { bounded.append({}); });
    auto excessive = std::vector<std::byte>(100, std::byte{0});
    excessive[0] = std::byte{255};
    excessive[1] = std::byte{216};
    excessive[98] = std::byte{255};
    excessive[99] = std::byte{217};
    rejects([&] { bounded.append(excessive); });
    bounded.append(packet); // Prior failures must not consume its planned frame.
    rejects([&] { bounded.append(packet); });
    const auto movie = bounded.finish();
    const auto frames = list(std::span(movie).subspan(12), "movi");
    check(frames.size() == 14 && frames.back() == std::byte{},
          "Odd JPEG chunk padding is missing or included in its payload");
    rejects([&] { (void)bounded.finish(); });
    rejects([&] { bounded.append(packet); });
}
void actual(const std::filesystem::path& root) {
    constexpr unsigned width = 7, height = 5, count = 3;
    const auto rate = avi_movie_frame_rate(30000. / 1001);
    AviMovie movie(width, height, rate, count);
    std::vector<std::vector<std::byte>> packets;
    for (unsigned frame = 0; frame < count; ++frame) {
        std::vector<unsigned char> rgba(width * height * 4);
        for (unsigned i = 0; i < width * height; ++i) {
            rgba[4 * i] = static_cast<unsigned char>(30 + frame * 70);
            rgba[4 * i + 1] = static_cast<unsigned char>(i * 4);
            rgba[4 * i + 2] = static_cast<unsigned char>(220 - frame * 60);
            rgba[4 * i + 3] = 255;
        }
        ForgeImageBytes encoded{};
        check(forge_jpeg_encode_rgba8(rgba.data(), rgba.size(), width, height, width * 4, 90,
                                      FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded),
              encoded.error);
        const auto* start = reinterpret_cast<const std::byte*>(encoded.bytes);
        packets.emplace_back(start, start + encoded.size);
        forge_image_bytes_free(&encoded);
        movie.append(packets.back());
        save(root / (std::to_string(frame) + ".jpg"), packets.back());
    }
    const auto bytes = movie.finish();
    check(tag(bytes, 0, "RIFF") && tag(bytes, 8, "AVI ") && integer(bytes, 4) == bytes.size() - 8,
          "Movie has the wrong form or declared file size");
    const auto data = std::span(bytes).subspan(12);
    const auto header = list(data, "hdrl"), main = chunk(header, "avih");
    check(main.size() == 56 && integer(main, 16) == count && integer(main, 24) == 1 &&
              integer(main, 32) == width && integer(main, 36) == height &&
              (integer(main, 12) & 0x10),
          "Movie dimensions, frame count, streams or index flag differ");
    const auto stream = list(header, "strl"), description = chunk(stream, "strh");
    check(description.size() == 56 && tag(description, 0, "vids") && tag(description, 4, "MJPG") &&
              integer(description, 20) == rate.scale && integer(description, 24) == rate.rate &&
              integer(description, 32) == count,
          "Movie stream lost its codec or fractional timebase");
    const auto frames = list(data, "movi"), index = chunk(data, "idx1");
    check(index.size() == count * 16, "Movie index is incomplete");
    for (unsigned frame = 0; frame < count; ++frame) {
        check(tag(index, frame * 16, "00dc") && integer(index, frame * 16 + 4) == 0x10,
              "Movie frame lost its stream or keyframe marker");
        const auto offset = integer(index, frame * 16 + 8);
        const auto size = integer(index, frame * 16 + 12);
        check(offset >= 4 && size == packets[frame].size() && tag(frames, offset - 4, "00dc") &&
                  integer(frames, offset) == size,
              "Movie index does not locate its original frame");
        const auto actual = frames.subspan(offset + 4, size);
        check(std::equal(actual.begin(), actual.end(), packets[frame].begin()),
              "Movie changed JPEG bytes or frame order");
    }
    save(root / "movie.avi", bytes);
    std::ofstream(root / "receipt.json") << nlohmann::json{
        {"width", width},
        {"height", height},
        {"frames", count},
        {"rate", rate.rate},
        {"scale", rate.scale}}.dump(2);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need movie test output directory");
        const auto root = std::filesystem::absolute(argv[1]) / forge::AssetId::generate().str();
        std::filesystem::create_directories(root);
        timing();
        boundaries();
        actual(root);
        std::cout << "Movie timing, bounds, indexed native JPEG output PASS: " << root << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
