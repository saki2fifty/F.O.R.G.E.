#include "audio_convolution.hpp"
#include <iostream>
#include <limits>

using forge::detail::AudioConvolution;
using forge::detail::AudioConvolutionKernel;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid impulse admitted");
}
void oracle(std::size_t taps, unsigned channels, float wet) {
    std::vector<float> impulse(taps * channels), input((taps + 1301) * 2);
    for (std::size_t i = 0; i < impulse.size(); ++i)
        impulse[i] = float(std::sin(double(i * 7 + 1)) * .025);
    for (std::size_t frame = 0; frame < 1301; ++frame) {
        input[2 * frame] = float(std::sin(double(frame) * .11) * .3);
        input[2 * frame + 1] = float(std::cos(double(frame) * .37) * .2);
    }
    auto kernel = std::make_shared<AudioConvolutionKernel>(impulse, channels);
    AudioConvolution whole(kernel, wet), split(kernel, wet), inplace(kernel, wet);
    std::vector<float> output(input.size()), chunks(output.size()), aliases = input;
    whole.process(input.data(), output.data(), input.size() / 2);
    constexpr std::array sizes{1u, 255u, 7u, 513u, 31u};
    std::size_t cursor = 0, step = 0;
    while (cursor < input.size() / 2) {
        const auto count =
            std::min<std::size_t>(sizes[step++ % sizes.size()], input.size() / 2 - cursor);
        split.process(input.data() + cursor * 2, chunks.data() + cursor * 2, count);
        inplace.process(aliases.data() + cursor * 2, aliases.data() + cursor * 2, count);
        cursor += count;
    }
    check(output == chunks && output == aliases, "Callback boundaries changed convolution");
    for (std::size_t frame = 0; frame < input.size() / 2; ++frame)
        for (unsigned channel = 0; channel < 2; ++channel) {
            double reference = 0;
            for (std::size_t tap = 0; tap < taps && tap <= frame; ++tap)
                reference += double(impulse[tap * channels + (channels == 1 ? 0 : channel)]) *
                             input[2 * (frame - tap) + channel];
            reference = (1 - wet) * input[2 * frame + channel] + wet * reference;
            check(std::abs(reference - output[2 * frame + channel]) < 2e-6,
                  "Convolution differs from independent time-domain sum");
        }
    whole.clear();
    std::fill(output.begin(), output.end(), 0);
    whole.process(nullptr, output.data(), output.size() / 2);
    check(std::ranges::all_of(output, [](float value) { return value == 0; }),
          "Clear retained convolution history");
    check(whole.tail_frames() == taps - 1, "Incorrect impulse tail bound");
}
void isolation_and_limits() {
    std::vector<float> impulse(AudioConvolutionKernel::max_frames * 2);
    impulse[0] = .4f;
    impulse[2 * 256] = -.2f;
    impulse[impulse.size() - 2] = .1f;
    impulse[1] = .8f;
    impulse[impulse.size() - 1] = -.3f;
    auto kernel = std::make_shared<AudioConvolutionKernel>(impulse, 2);
    AudioConvolution first(kernel, 1), second(kernel, 1);
    check(kernel->bytes() + 2 * first.bytes() < 32 * 1024 * 1024,
          "Single maximum stereo impulse exceeds graph memory budget");
    std::vector<float> output(2 * (AudioConvolutionKernel::max_frames + 1));
    float pulse[]{1, 0};
    first.process(pulse, output.data(), 1);
    first.process(nullptr, output.data() + 2, output.size() / 2 - 1);
    for (std::size_t frame = 0; frame < output.size() / 2; ++frame) {
        const auto expected = frame < impulse.size() / 2 ? impulse[2 * frame] : 0;
        check(std::abs(output[2 * frame] - expected) < 2e-6 && output[2 * frame + 1] == 0,
              "Maximum impulse tail lost a sample or crossed stereo channels");
    }
    second.process(nullptr, output.data(), output.size() / 2);
    check(std::ranges::all_of(output, [](float value) { return value == 0; }),
          "Shared kernel leaked mutable state between processors");
    float invalid[]{std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity()};
    second.process(invalid, output.data(), 1);
    second.process(nullptr, output.data() + 2, output.size() / 2 - 1);
    check(std::ranges::all_of(output, [](float value) { return value == 0; }),
          "Nonfinite input poisoned convolution history");
    rejects([] { AudioConvolutionKernel value({}, 1); });
    rejects([] { AudioConvolutionKernel value(std::array{1.f}, 0); });
    rejects([] { AudioConvolutionKernel value(std::array{1.f}, 2); });
    rejects([] { AudioConvolutionKernel value(std::array{1.f, 2.f, 3.f}, 3); });
    rejects([&] { AudioConvolutionKernel value{std::span(invalid), 1}; });
    rejects([] { AudioConvolutionKernel value(std::vector<float>(65, 1), 1); });
    rejects([] { AudioConvolutionKernel value(std::vector<float>(96001), 1); });
    rejects([&] { AudioConvolution value(kernel, -1); });
    rejects([&] { AudioConvolution value(kernel, std::numeric_limits<float>::quiet_NaN()); });
    rejects([] { AudioConvolution value({}, 1); });
}
void bursts_and_silence() {
    std::vector<float> response(513);
    response[0] = .5f;
    response[256] = -.25f;
    response[512] = .125f;
    auto kernel = std::make_shared<AudioConvolutionKernel>(response, 1);
    AudioConvolution processor(kernel, 1);
    std::vector<float> input(6000), output(input.size());
    constexpr std::array pulses{0u, 1000u, 1673u};
    for (auto pulse : pulses)
        input[2 * pulse] = .3f;
    for (std::size_t frame = 0; frame < input.size() / 2;) {
        const auto count = std::min<std::size_t>(19, input.size() / 2 - frame);
        processor.process(input.data() + 2 * frame, output.data() + 2 * frame, count);
        frame += count;
    }
    for (std::size_t frame = 0; frame < input.size() / 2; ++frame) {
        double expected = 0;
        for (auto pulse : pulses)
            if (frame >= pulse && frame - pulse < response.size())
                expected += .3f * response[frame - pulse];
        check(std::abs(output[2 * frame] - expected) < 2e-6 && output[2 * frame + 1] == 0,
              "Silence retirement changed reactivated impulse timing or retained old state");
    }
}
} // namespace
int main() {
    try {
        for (auto size : {1u, 17u, 255u, 256u, 257u, 511u, 512u, 513u, 1025u})
            for (auto channels : {1u, 2u})
                for (auto wet : {0.f, .35f, 1.f})
                    oracle(size, channels, wet);
        isolation_and_limits();
        bursts_and_silence();
        std::cout << "Impulse/direct oracle, callback/alias, maximum tail, stereo/domain isolation "
                     "and admission passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
