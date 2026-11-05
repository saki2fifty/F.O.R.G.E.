#include "audio_stream_file.hpp"
#include "native_io_path.hpp"
#include "worker_staging.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace forge::asset_detail {
AudioStreamBudget::AudioStreamBudget(std::filesystem::path temporary_root,
                                     AudioRetirement::Sink sink)
    : root(std::filesystem::absolute(std::move(temporary_root))), retirement(std::move(sink)) {}
struct AudioStreamFile::State {
    std::shared_ptr<AudioStreamBudget> budget;
    std::unique_ptr<WorkerStaging> stage;
    std::filesystem::path file;
    AudioClipInfo info;
    std::uint64_t charged_bytes = 0;
    bool charged_file = false;
    ~State() {
        stage.reset();
        if (charged_bytes)
            budget->bytes.fetch_sub(charged_bytes, std::memory_order_relaxed);
        if (charged_file)
            budget->files.fetch_sub(1, std::memory_order_relaxed);
    }
};
AudioStreamFile::AudioStreamFile(std::unique_ptr<State> state) : state_(std::move(state)) {}
AudioStreamFile::~AudioStreamFile() = default;
const std::filesystem::path& AudioStreamFile::path() const { return state_->file; }
AudioClipInfo AudioStreamFile::info() const { return state_->info; }
std::uint64_t AudioStreamFile::byte_size() const { return state_->charged_bytes; }
std::shared_ptr<const AudioStreamFile>
AudioStreamFile::create(std::span<const std::byte> pcm, std::shared_ptr<AudioStreamBudget> budget,
                        std::stop_token stop) {
    const auto info = validate_audio_clip_bytes(pcm);
    if (!budget || info.rate != audio_stream_sample_rate || info.frames < audio_stream_sample_rate)
        throw std::runtime_error(
            "Audio streaming needs bounded 48000 Hz PCM of at least one second");
    if (stop.stop_requested())
        throw std::runtime_error("Audio stream backing preparation cancelled");
    std::call_once(budget->cleanup, [&] {
        std::filesystem::create_directories(native_io_path(budget->root));
        try {
            ProjectLease lease(budget->root);
            (void)cleanup_worker_stages(lease, stop);
        } catch (const std::exception&) {
            // Other live owners may hold maintenance. Each stage remains
            // independently locked; a later maintenance pass can retry.
            budget->cleanup_pending.store(true, std::memory_order_relaxed);
        }
    });
    auto state = std::make_unique<State>();
    state->budget = std::move(budget);
    state->info = info;
    if (state->budget->files.fetch_add(1, std::memory_order_relaxed) >= max_audio_stream_backings) {
        state->budget->files.fetch_sub(1, std::memory_order_relaxed);
        throw std::runtime_error("Audio temporary backing count exceeds 64");
    }
    state->charged_file = true;
    constexpr unsigned wave_header_bytes = 58;
    const auto samples = pcm.subspan(audio_pcm_header_bytes);
    const std::uint64_t bytes = wave_header_bytes + samples.size();
    auto previous = state->budget->bytes.load(std::memory_order_relaxed);
    for (;;) {
        if (previous > max_audio_stream_backing_bytes ||
            bytes > max_audio_stream_backing_bytes - previous)
            throw std::runtime_error("Audio temporary backing budget exceeds 256 MiB");
        if (state->budget->bytes.compare_exchange_weak(previous, previous + bytes,
                                                       std::memory_order_relaxed))
            break;
    }
    state->charged_bytes = bytes;
    state->stage = std::make_unique<WorkerStaging>(state->budget->root, "audio-stream");
    state->file = state->stage->path / "audio.wav";
    std::array<unsigned char, wave_header_bytes> header{};
    const auto tag = [&](unsigned at, const char* value) {
        for (unsigned i = 0; i < 4; ++i)
            header[at + i] = static_cast<unsigned char>(value[i]);
    };
    const auto put = [&](unsigned at, std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            header[at + i] = static_cast<unsigned char>(value >> (i * 8));
    };
    tag(0, "RIFF");
    put(4, std::uint32_t(bytes - 8), 4);
    tag(8, "WAVE");
    tag(12, "fmt ");
    put(16, 18, 4);
    put(20, 3, 2);
    put(22, info.channels, 2);
    put(24, info.rate, 4);
    put(28, info.rate * info.channels * 4, 4);
    put(32, info.channels * 4, 2);
    put(34, 32, 2);
    put(36, 0, 2); // WAVEFORMATEX: no extra format data.
    tag(38, "fact");
    put(42, 4, 4);
    put(46, std::uint32_t(info.frames), 4);
    tag(50, "data");
    put(54, std::uint32_t(samples.size()), 4);
    std::ofstream file(native_io_path(state->file), std::ios::binary);
    file.write(reinterpret_cast<const char*>(header.data()), header.size());
    for (std::size_t offset = 0; offset < samples.size();) {
        if (stop.stop_requested())
            throw std::runtime_error("Audio stream backing preparation cancelled");
        const auto count = std::min<std::size_t>(65536, samples.size() - offset);
        file.write(reinterpret_cast<const char*>(samples.data() + offset), std::streamsize(count));
        if (!file)
            throw std::runtime_error("Cannot write temporary audio stream backing");
        offset += count;
    }
    file.close();
    if (!file || stop.stop_requested())
        throw std::runtime_error("Audio stream backing completion failed or was cancelled");
    const auto retirement = state->budget->retirement;
    return std::shared_ptr<const AudioStreamFile>(
        new AudioStreamFile(std::move(state)), [retirement](const AudioStreamFile* value) {
            retirement.retire(const_cast<AudioStreamFile*>(value),
                              [](void* p) noexcept { delete static_cast<AudioStreamFile*>(p); });
        });
}
} // namespace forge::asset_detail
