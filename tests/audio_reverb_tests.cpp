#include "audio_reverb.hpp"
#include <iostream>
#include <limits>
#include <span>

using forge::detail::AudioReverb;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid reverb configuration admitted");
}
double energy(std::span<const float> samples) {
    double result = 0;
    for (float sample : samples) {
        check(std::isfinite(sample), "Reverb produced nonfinite output");
        result += double(sample) * sample;
    }
    return result;
}
void impulse() {
    constexpr unsigned rate = 48000;
    std::vector<float> input(rate * 2), output(input.size()), chunks(input.size());
    input[0] = 1;
    AudioReverb a(rate, 2, 1.5f, .3f, .4f), b(rate, 2, 1.5f, .3f, .4f);
    a.process(input.data(), output.data(), rate);
    for (unsigned frame = 0; frame < rate;) {
        const auto count = std::min(137u, rate - frame);
        b.process(input.data() + frame * 2, chunks.data() + frame * 2, count);
        frame += count;
    }
    check(output == chunks, "Reverb changes with callback block boundaries");
    check(output[0] == .6f && output[1] == 0, "Reverb changed immediate dry routing");
    unsigned echoes = 0;
    for (unsigned frame = 1; frame < rate; ++frame) {
        check(output[frame * 2 + 1] == 0, "Independent stereo reverb leaked across channels");
        echoes += output[frame * 2] != 0;
    }
    check(echoes > 10000 && energy(output) > .36, "Impulse did not develop a diffuse tail");
    a.clear();
    a.process(input.data(), chunks.data(), rate);
    check(chunks == output, "Cleared reverb retained old diffusion history");
    AudioReverb bypass(rate, 2, 1.5f, .3f, 0);
    bypass.process(input.data(), chunks.data(), rate);
    check(chunks == input, "Zero wet reverb is not transparent");
    auto inplace = input;
    AudioReverb alias(rate, 2, 1.5f, .3f, .4f);
    alias.process(inplace.data(), inplace.data(), rate);
    check(inplace == output, "In-place processing corrupted interleaved input");
}
void decay_and_damping() {
    constexpr unsigned rate = 48000;
    std::vector<float> impulse(rate * 2), short_output(impulse.size()), long_output(impulse.size()),
        damped_output(impulse.size());
    impulse[0] = 1;
    AudioReverb short_room(rate, 1, .2f, 0, 1), long_room(rate, 1, 2, 0, 1),
        damped(rate, 1, 2, .9f, 1);
    short_room.process(impulse.data(), short_output.data(), impulse.size());
    long_room.process(impulse.data(), long_output.data(), impulse.size());
    damped.process(impulse.data(), damped_output.data(), impulse.size());
    const auto late = [](const auto& samples) {
        return energy(std::span(samples).subspan(rate / 2));
    };
    check(late(long_output) > late(short_output) * 100,
          "Longer decay did not retain more late energy");
    check(energy(damped_output) < energy(long_output) && damped_output != long_output,
          "Damping did not diffuse and attenuate impulse high frequencies");
    AudioReverb maximum(rate, 2, 10, .95f, 1);
    check(maximum.bytes() < 64 * 1024 && maximum.tail_frames() == 984000,
          "Reverb memory or retirement bound changed");
    std::vector<float> first(1024 * 2), tail(first.size());
    first[0] = first[1] = 1;
    maximum.process(first.data(), tail.data(), 1024);
    for (std::uint64_t frames = 1024; frames < maximum.tail_frames(); frames += 1024)
        maximum.process(nullptr, tail.data(), 1024);
    check(energy(tail) < 1e-12, "Maximum admitted reverb outlived its conservative tail bound");
    first[0] = std::numeric_limits<float>::infinity();
    first[1] = std::numeric_limits<float>::quiet_NaN();
    maximum.process(first.data(), tail.data(), 1024);
    check(std::isfinite(energy(tail)), "Nonfinite input poisoned the delay network");
}
void admission() {
    reject([] { AudioReverb bad(0, 2, 1, 0, .2f); });
    reject([] { AudioReverb bad(192001, 2, 1, 0, .2f); });
    reject([] { AudioReverb bad(48000, 0, 1, 0, .2f); });
    reject([] { AudioReverb bad(48000, 3, 1, 0, .2f); });
    reject([] { AudioReverb bad(48000, 2, .01f, 0, .2f); });
    reject([] { AudioReverb bad(48000, 2, 11, 0, .2f); });
    reject([] { AudioReverb bad(48000, 2, 1, 1, .2f); });
    reject([] { AudioReverb bad(48000, 2, 1, 0, -1); });
    reject([] { AudioReverb bad(48000, 2, 1, 0, std::numeric_limits<float>::quiet_NaN()); });
    for (unsigned rate : {8000u, 44100u, 48000u, 96000u, 192000u}) {
        AudioReverb value(rate, 2, .1f, 0, 1);
        std::vector<float> samples(2048);
        value.process(nullptr, samples.data(), 1024);
        check(energy(samples) == 0 && value.bytes() < 256 * 1024,
              "Supported rate exceeded memory bound or produced output from silence");
    }
}
} // namespace
int main() {
    try {
        impulse();
        decay_and_damping();
        admission();
        std::cout << "Private reverb impulse, callback boundaries, stereo, decay, damping and "
                     "bounds passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
