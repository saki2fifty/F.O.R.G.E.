#include "audio_stream_source.hpp"
#include "fixtures/audio_stream_vfs.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <forge/identity.hpp>
#include <iostream>
#include <source_location>
#include <thread>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void checked(ma_result result) {
    if (result != MA_SUCCESS)
        throw std::runtime_error(ma_result_description(result));
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid native stream was accepted");
}
template <class F>
void until(F ready, const std::source_location& location = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Native stream preparation stalled at line " +
                                     std::to_string(location.line()));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Engine {
    ma_engine value{};
    explicit Engine(forge::test::AudioStreamVfs& vfs) {
        auto config = ma_engine_config_init();
        config.noDevice = config.noAutoStart = MA_TRUE;
        config.channels = 2;
        config.sampleRate = 48000;
        config.pResourceManagerVFS = &vfs;
        checked(ma_engine_init(&config, &value));
    }
    ~Engine() { ma_engine_uninit(&value); }
};
struct Buffer {
    ma_audio_buffer_ref value{};
    Buffer(const forge::asset_detail::AudioClipData& clip, std::uint64_t initial) {
        checked(ma_audio_buffer_ref_init(ma_format_f32, clip.channels, clip.pcm.data(), clip.frames,
                                         &value));
        value.sampleRate = clip.rate;
        checked(ma_audio_buffer_ref_seek_to_pcm_frame(&value, initial));
    }
    ~Buffer() { ma_audio_buffer_ref_uninit(&value); }
};
struct Sound {
    ma_sound value{};
    Sound(Engine& engine, ma_data_source* source, bool loop, float pitch) {
        checked(ma_sound_init_from_data_source(&engine.value, source, 0, nullptr, &value));
        ma_sound_set_spatialization_enabled(&value, MA_FALSE);
        ma_sound_set_pitch(&value, pitch);
        ma_sound_set_looping(&value, loop);
        checked(ma_sound_start(&value));
    }
    ~Sound() { ma_sound_uninit(&value); }
};
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    using namespace forge::asset_detail;
    try {
        require(argc == 2, "Need native audio stream scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        for (const unsigned channels : {1u, 2u}) {
            test::AudioStreamVfs vfs;
            Engine engine(vfs);
            AudioRetirement retirement;
            const auto budget = std::make_shared<AudioStreamBudget>(root, retirement.sink());
            const auto slots = std::make_shared<AudioStreamSlots>();
            AudioClipData clip;
            clip.frames = 144137;
            clip.rate = 48000;
            clip.channels = channels;
            clip.pcm.resize(clip.frames * channels);
            for (std::uint64_t frame = 0; frame < clip.frames; ++frame)
                for (unsigned c = 0; c < channels; ++c)
                    clip.pcm[frame * channels + c] =
                        float(int((frame * 17 + c * 231) % 1021) - 510) / 1024;
            auto file = AudioStreamFile::create(encode_audio_clip(clip), budget);
            for (const bool loop : {false, true})
                for (const float pitch : {.5f, 1.f, 2.f})
                    for (const std::uint64_t initial : {0ull, 50001ull, 132000ull}) {
                        auto source = AudioStreamSource::create(
                            *ma_engine_get_resource_manager(&engine.value), file, slots,
                            retirement.sink(), loop, initial);
                        require(bool(source), "Fresh native stream slot remained pending");
                        until([&] { return source->ready(); });
                        require(source->cursor() == initial &&
                                    source->page_bytes() == 2ull * 48000 * channels * 4,
                                "Prepared stream ignored its initial position or page bounds");
                        test::AudioStreamVfs reference_vfs;
                        Engine reference(reference_vfs);
                        Buffer buffer(clip, initial);
                        Sound actual(engine, source->data_source(), loop, pitch);
                        Sound expected(reference, &buffer.value, loop, pitch);
                        std::array<float, 512> streamed{}, resident{};
                        for (unsigned frame = 0; frame < 48000 * 8; frame += 256) {
                            until([&] {
                                const auto cursor = source->cursor();
                                const auto remaining =
                                    cursor < clip.frames ? clip.frames - cursor : 0;
                                const auto needed =
                                    loop ? 48000 : std::min<std::uint64_t>(48000, remaining);
                                return source->available_frames() >= needed;
                            });
                            test::AudioStreamVfs::consuming = true;
                            checked(ma_engine_read_pcm_frames(&engine.value, streamed.data(), 256,
                                                              nullptr));
                            checked(ma_engine_read_pcm_frames(&reference.value, resident.data(),
                                                              256, nullptr));
                            test::AudioStreamVfs::consuming = false;
                            for (unsigned i = 0; i < streamed.size(); ++i)
                                if (std::abs(streamed[i] - resident[i]) > 1e-6)
                                    throw std::runtime_error(
                                        "Streamed mixed PCM differs: channels=" +
                                        std::to_string(channels) + " loop=" + std::to_string(loop) +
                                        " pitch=" + std::to_string(pitch) +
                                        " initial=" + std::to_string(initial) +
                                        " frame=" + std::to_string(frame + i / 2));
                        }
                        require(source->underruns() == 0 && source->read_error() == MA_SUCCESS,
                                "Valid prebuffered stream reported starvation or premature EOF");
                    }
            require(vfs.owner_io == 0 && vfs.consumer_io == 0 && vfs.worker_io > 0,
                    "Native stream file IO crossed onto the owner or consuming mixer thread");
            until([&] { return slots->reserved == 0; });
            {
                auto source =
                    AudioStreamSource::create(*ma_engine_get_resource_manager(&engine.value), file,
                                              slots, retirement.sink(), true);
                until([&] { return source->ready(); });
                Sound sound(engine, source->data_source(), true, 1);
                vfs.delay_read_ms = 250;
                std::array<float, 512> pcm{};
                for (unsigned i = 0; i < 1000; ++i)
                    checked(ma_engine_read_pcm_frames(&engine.value, pcm.data(), 256, nullptr));
                require(source->underruns() > 0 && source->read_error() == MA_SUCCESS,
                        "Starved native pages were not observed independently of read failure");
                until([&] { return source->available_frames() >= 48000; });
                const auto underruns = source->underruns();
                checked(ma_engine_read_pcm_frames(&engine.value, pcm.data(), 256, nullptr));
                require(source->underruns() == underruns &&
                            std::ranges::any_of(pcm, [](float v) { return std::abs(v) > .01f; }),
                        "Native stream did not recover after worker starvation");
                vfs.fail_reads = true;
                unsigned fault_mixes = 0;
                try {
                    until([&] {
                        ++fault_mixes;
                        checked(ma_engine_read_pcm_frames(&engine.value, pcm.data(), 256, nullptr));
                        return source->read_error() != MA_SUCCESS;
                    });
                } catch (const std::exception& error) {
                    throw std::runtime_error(
                        std::string(error.what()) + " channels=" + std::to_string(channels) +
                        " failed_reads=" + std::to_string(vfs.failed_reads.load()) +
                        " mixes=" + std::to_string(fault_mixes) +
                        " cursor=" + std::to_string(source->cursor()) +
                        " available=" + std::to_string(source->available_frames()) +
                        " underruns=" + std::to_string(source->underruns()) +
                        " playing=" + std::to_string(ma_sound_is_playing(&sound.value)) +
                        " ended=" + std::to_string(ma_sound_at_end(&sound.value)));
                }
                require(source->read_error() != MA_BUSY,
                        "Permanent native read failure was confused with temporary starvation");
            }
            until([&] { return slots->reserved == 0; });
            vfs.fail_reads = true;
            {
                auto source =
                    AudioStreamSource::create(*ma_engine_get_resource_manager(&engine.value), file,
                                              slots, retirement.sink(), false);
                bool failed = false;
                until([&] {
                    try {
                        (void)source->ready();
                    } catch (const std::exception&) {
                        failed = true;
                    }
                    return failed;
                });
                require(failed, "Native preparation silently accepted IO failure");
            }
            vfs.fail_reads = false;
            until([&] { return slots->reserved == 0; });
            std::vector<AudioStreamSource::Pointer> limit;
            for (unsigned i = 0; i < max_audio_streams; ++i)
                limit.push_back(
                    AudioStreamSource::create(*ma_engine_get_resource_manager(&engine.value), file,
                                              slots, retirement.sink(), true));
            rejects([&] {
                (void)AudioStreamSource::create(*ma_engine_get_resource_manager(&engine.value),
                                                file, slots, retirement.sink(), true);
            });
            rejects([&] {
                (void)AudioStreamSource::create(*ma_engine_get_resource_manager(&engine.value),
                                                file, slots, retirement.sink(), true, clip.frames);
            });
            limit.clear(); // Includes requests still preparing; retirement must join their jobs.
            file.reset();
            retirement.close();
            require(!slots->reserved && !slots->retiring && !slots->page_budget_bytes &&
                        !budget->bytes && !budget->files,
                    "Native stream teardown retained slots, backing or native work");
        }
        std::cout << "36 native mono/stereo loop/pitch/initial-position mixed PCM cases, "
                     "starvation/recovery, page/slot "
                     "bounds, worker-only IO and joined cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
