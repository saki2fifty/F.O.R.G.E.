#include "audio_capture_access.hpp"
#include "audio_capture_buffer.hpp"
#include "audio_test_fixture.hpp"
#include <array>
#include <atomic>
#include <limits>

using forge::asset_detail::AudioCaptureBuffer;
namespace {
std::vector<float> ramp(unsigned start, unsigned frames) {
    std::vector<float> result;
    for (unsigned i = start; i < start + frames; ++i) {
        result.push_back(float(i));
        result.push_back(-float(i));
    }
    return result;
}
void bounded_prefix() {
    reject([] { AudioCaptureBuffer bad(0); });
    reject([] { AudioCaptureBuffer bad(AudioCaptureBuffer::maximum_queued_frames + 1); });
    AudioCaptureBuffer queue(8);
    reject([&] { (void)queue.read(1); });
    queue.start();
    reject([&] { queue.start(); });
    const auto first = ramp(0, 6), second = ramp(6, 5);
    queue.observe(first.data(), 6);
    check(queue.read(4) == ramp(0, 4), "Live PCM initial prefix changed");
    queue.observe(second.data(), 5);
    check(queue.read(8) == ramp(4, 7), "Wrapped live PCM changed sample order");
    reject([&] { (void)queue.read(0); });
    reject([&] { (void)queue.read(AudioCaptureBuffer::maximum_read_frames + 1); });
    queue.stop();
    auto state = queue.progress();
    check(!state.active && state.captured_frames == 11 && state.read_frames == 11 &&
              state.queued_frames == 0 && state.failure == AudioCaptureBuffer::Failure::None,
          "Stopped live PCM counters did not match its exact prefix");
    queue.observe(first.data(), 6);
    check(queue.read(8).empty() && queue.progress().captured_frames == 11,
          "A stopped capture consumed later device samples");
    queue.start();
    queue.observe(first.data(), 6);
    queue.observe(second.data(), 3);
    state = queue.progress();
    check(!state.active && state.failure == AudioCaptureBuffer::Failure::Overflow &&
              state.captured_frames == 6,
          "Overflow did not terminate at the completed callback prefix");
    reject([&] { queue.start(); });
    check(queue.read(8) == first, "Overflow discarded or overwrote its retained prefix");
    queue.start();
    auto invalid = ramp(20, 4);
    invalid[5] = std::numeric_limits<float>::quiet_NaN();
    queue.observe(first.data(), 2);
    queue.observe(invalid.data(), 4);
    check(queue.progress().failure == AudioCaptureBuffer::Failure::Nonfinite &&
              queue.read(8) == ramp(0, 2),
          "Invalid PCM entered the queue or discarded an earlier complete prefix");
    queue.start();
    queue.observe(nullptr, 0);
    queue.observe(nullptr, 1);
    check(queue.progress().failure == AudioCaptureBuffer::Failure::Nonfinite,
          "A missing nonempty callback buffer was accepted");
    queue.start();
    queue.observe(first.data(), std::numeric_limits<std::uint64_t>::max());
    check(queue.progress().failure == AudioCaptureBuffer::Failure::Overflow &&
              queue.progress().captured_frames == 0 && queue.progress().generation == 5,
          "Oversized callback was dereferenced or restart generation was lost");
    queue.stop();
}
void concurrent_prefix() {
    AudioCaptureBuffer queue(512);
    constexpr unsigned total = 200000;
    std::atomic<unsigned> consumed{0};
    queue.start();
    std::jthread producer([&](std::stop_token stop) {
        for (unsigned start = 0; start < total && !stop.stop_requested();) {
            while (start - consumed.load() >= 128 && !stop.stop_requested())
                std::this_thread::yield();
            if (stop.stop_requested())
                return;
            const auto count = std::min(17u, total - start);
            const auto data = ramp(start, count);
            queue.observe(data.data(), count);
            start += count;
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (consumed.load() != total) {
        check(std::chrono::steady_clock::now() < deadline, "Concurrent PCM prefix timed out");
        const auto data = queue.read(31);
        check(data == ramp(consumed.load(), unsigned(data.size() / 2)),
              "Concurrent PCM was repeated, skipped or overwritten");
        consumed.fetch_add(unsigned(data.size() / 2));
        if (data.empty())
            std::this_thread::yield();
    }
    producer.join();
    queue.stop();
    const auto state = queue.progress();
    check(state.failure == AudioCaptureBuffer::Failure::None && state.captured_frames == total &&
              state.read_frames == total && state.queued_frames == 0,
          "Concurrent callback counters did not settle to the retained prefix");
}
void concurrent_restarts() {
    AudioCaptureBuffer queue;
    std::atomic<unsigned> callbacks{0};
    std::jthread producer([&](std::stop_token stop) {
        std::array<float, 64> pcm{};
        while (!stop.stop_requested()) {
            queue.observe(pcm.data(), pcm.size() / 2);
            callbacks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    for (unsigned i = 0; i < 64; ++i) {
        queue.start();
        const auto before = callbacks.load();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (callbacks.load() == before) {
            check(std::chrono::steady_clock::now() < deadline, "Restart callback timed out");
            std::this_thread::yield();
        }
        queue.stop();
        const auto state = queue.progress();
        std::size_t drained = 0;
        while (queue.progress().queued_frames)
            drained += queue.read(AudioCaptureBuffer::maximum_read_frames).size();
        check(!state.active && state.generation == i + 1 &&
                  state.failure == AudioCaptureBuffer::Failure::None &&
                  drained == state.captured_frames * 2,
              "Concurrent restart mixed generations or lost its completed prefix");
    }
    producer.request_stop();
    producer.join();
}
void device_capture(const std::filesystem::path& root, AssetId clip) {
    Fixture offline(root);
    reject([&] { AudioCaptureAccess::begin(*offline.audio); });
    Fixture device(root, 1, false, {}, AudioOutput::NullDeviceTest);
    device.load(source(clip));
    device.simulation.audio_paused(false);
    AudioCaptureAccess::begin(*device.audio);
    reject([&] { AudioCaptureAccess::begin(*device.audio); });
    std::vector<float> samples;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (samples.size() < 9600) {
        check(std::chrono::steady_clock::now() < deadline, "Actual device capture timed out");
        auto chunk = AudioCaptureAccess::read(*device.audio, 4096);
        samples.insert(samples.end(), chunk.begin(), chunk.end());
        if (chunk.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(std::ranges::any_of(samples, [](float value) { return std::abs(value) > .001f; }),
          "Existing device mixer capture was silent");
    AudioCaptureAccess::end(*device.audio);
    auto status = AudioCaptureAccess::status(*device.audio);
    const auto final_frames = status.at("captured_frames").get<std::uint64_t>();
    while (status.at("queued_frames") != 0) {
        auto chunk = AudioCaptureAccess::read(*device.audio, 4096);
        samples.insert(samples.end(), chunk.begin(), chunk.end());
        status = AudioCaptureAccess::status(*device.audio);
    }
    check(samples.size() == final_frames * 2 && status.at("read_frames") == final_frames &&
              status.at("state") == "stopped",
          "Stopping device capture lost or invented mixed samples");
    const auto device_before = device.audio->status().at("mixed_frames").get<std::uint64_t>();
    const auto keep_playing = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (device.audio->status().at("mixed_frames").get<std::uint64_t>() == device_before) {
        check(std::chrono::steady_clock::now() < keep_playing, "Capture Stop stopped the mixer");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(AudioCaptureAccess::read(*device.audio, 4096).empty() &&
              AudioCaptureAccess::status(*device.audio).at("captured_frames") == final_frames,
          "A stopped capture retained later live sound");
    std::atomic<bool> refused{false};
    std::thread wrong([&] {
        try {
            (void)AudioCaptureAccess::status(*device.audio);
        } catch (const std::exception&) {
            refused.store(true);
        }
    });
    wrong.join();
    check(refused.load(), "Live capture allowed another owner thread");
    device.audio->paused(true);
    AudioCaptureAccess::begin(*device.audio);
    device.audio->shutdown();
    status = AudioCaptureAccess::status(*device.audio);
    check(status.at("state") == "stopped" && status.at("generation") == 2,
          "Shutdown left the callback capture active");
    reject([&] { AudioCaptureAccess::begin(*device.audio); });
    AudioCaptureAccess::end(*device.audio);
    while (AudioCaptureAccess::status(*device.audio).at("queued_frames") != 0)
        (void)AudioCaptureAccess::read(*device.audio, 4096);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need a temporary audio project directory");
        bounded_prefix();
        concurrent_prefix();
        concurrent_restarts();
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        const auto clip = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        device_capture(root, clip.id);
        std::filesystem::remove_all(root);
        std::cout
            << "Bounded concurrent capture, actual null-test mix, retained prefix and shutdown "
               "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
