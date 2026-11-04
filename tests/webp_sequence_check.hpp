#pragma once
#include "image_pixels.hpp"
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/mux.h>
namespace forge::test_webp_sequence {
inline void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
inline void inspect(std::span<const std::byte> file, std::span<const Rgba8Pixels> frames,
                    std::span<const unsigned> durations) {
    check(!frames.empty() && frames.size() == durations.size(),
          "Invalid expected animation fixtures");
    const auto width_expected = frames.front().width, height_expected = frames.front().height;
    for (const auto& frame : frames)
        check(frame.width == width_expected && frame.height == height_expected &&
                  frame.rgba.size() == std::size_t(width_expected) * height_expected * 4,
              "Invalid expected animation frame shape");
    const WebPData data{reinterpret_cast<const std::uint8_t*>(file.data()), file.size()};
    const std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(WebPDemux(&data),
                                                                         WebPDemuxDelete);
    check(bool(demux) && WebPDemuxGetI(demux.get(), WEBP_FF_FRAME_COUNT) == frames.size() &&
              WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH) == width_expected &&
              WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT) == height_expected &&
              WebPDemuxGetI(demux.get(), WEBP_FF_LOOP_COUNT) == 0,
          "WebP animation lost frame count, canvas or infinite loop metadata");
    for (unsigned i = 0; i < frames.size(); ++i) {
        struct Iterator : WebPIterator {
            Iterator() : WebPIterator{} {}
            ~Iterator() { WebPDemuxReleaseIterator(this); }
        } frame;
        check(WebPDemuxGetFrame(demux.get(), int(i + 1), &frame) && frame.complete &&
                  frame.x_offset == 0 && frame.y_offset == 0 &&
                  frame.width == int(width_expected) && frame.height == int(height_expected) &&
                  frame.duration == int(durations[i]) && frame.blend_method == WEBP_MUX_NO_BLEND &&
                  frame.dispose_method == WEBP_MUX_DISPOSE_NONE,
              "WebP animation changed frame order, timing, canvas or compositing");
        int width = 0, height = 0;
        const std::unique_ptr<std::uint8_t, decltype(&WebPFree)> decoded(
            WebPDecodeRGBA(frame.fragment.bytes, frame.fragment.size, &width, &height), WebPFree);
        const auto& expected = frames[i];
        check(decoded && width == int(width_expected) && height == int(height_expected) &&
                  std::memcmp(decoded.get(), expected.rgba.data(), expected.rgba.size()) == 0,
              "Muxed lossless frame changed RGBA, transparent RGB or orientation");
    }
    WebPAnimDecoderOptions options;
    check(WebPAnimDecoderOptionsInit(&options), "Cannot initialize native animation decoder");
    options.color_mode = MODE_RGBA;
    options.use_threads = 0;
    const std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> animation(
        WebPAnimDecoderNew(&data, &options), WebPAnimDecoderDelete);
    check(bool(animation), "Native animation player rejected muxed output");
    unsigned cumulative = 0;
    for (unsigned i = 0; i < frames.size(); ++i) {
        cumulative += durations[i];
        std::uint8_t* rgba = nullptr;
        int timestamp = 0;
        check(WebPAnimDecoderHasMoreFrames(animation.get()) &&
                  WebPAnimDecoderGetNext(animation.get(), &rgba, &timestamp) && rgba &&
                  timestamp == int(cumulative),
              "Native animation playback timestamps drifted");
        const auto& expected = frames[i];
        for (unsigned j = 0; j < std::size_t(width_expected) * height_expected; ++j) {
            check(rgba[j * 4 + 3] == expected.rgba[j * 4 + 3],
                  "Animation frame retained old alpha");
            if (expected.rgba[j * 4 + 3])
                check(std::memcmp(rgba + j * 4, expected.rgba.data() + j * 4, 3) == 0,
                      "Animation full-canvas replacement retained old colors");
        }
    }
    check(!WebPAnimDecoderHasMoreFrames(animation.get()), "Animation gained an extra frame");
}
} // namespace forge::test_webp_sequence
