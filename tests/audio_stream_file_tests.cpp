#include "asset_bytes.hpp"
#include "audio_retirement.hpp"
#include "audio_stream_file.hpp"
#include "worker_staging.hpp"
#include <condition_variable>
#include <forge/identity.hpp>
#include <fstream>
#include <iostream>
#include <mutex>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid audio backing preparation was accepted");
}
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    using namespace forge::asset_detail;
    try {
        require(argc == 2, "Need audio backing scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        AudioClipData source;
        source.frames = source.rate = 48000;
        source.channels = 2;
        source.pcm.resize(source.frames * source.channels);
        for (std::size_t i = 0; i < source.pcm.size(); ++i)
            source.pcm[i] = float(int(i % 1021) - 510) / 1024;
        auto bytes = encode_audio_clip(source);
        auto budget = std::make_shared<AudioStreamBudget>(root);
        auto file = AudioStreamFile::create(bytes, budget);
        const auto owned_path = file->path();
        require(budget->files == 1 && budget->bytes == file->byte_size(),
                "Audio backing quota did not retain its owned file");
        bytes.clear();
        bytes.shrink_to_fit();
        const auto decoded = decode_audio_wav(read_bytes(owned_path, max_audio_source_bytes));
        require(decoded.pcm == source.pcm && decoded.frames == source.frames &&
                    decoded.rate == 48000 && decoded.channels == 2,
                "Owned audio backing changed PCM or depended on the caller byte lifetime");
        auto copy = file;
        file.reset();
        require(std::filesystem::exists(owned_path) && budget->files == 1,
                "Independent copied backing did not retain its file");
        {
            WorkerStaging orphan(root, "audio-stream");
            const auto orphan_path = orphan.path;
            std::ofstream(orphan.path / "audio.wav") << "stale temporary bytes";
            orphan.owner.reset();
            auto other_budget = std::make_shared<AudioStreamBudget>(root);
            auto other = AudioStreamFile::create(encode_audio_clip(source), other_budget);
            require(!std::filesystem::exists(orphan_path) && std::filesystem::exists(owned_path),
                    "Temporary maintenance removed a live owner or retained a recognized orphan");
            require(other_budget->files == 1 && other_budget->bytes == other->byte_size(),
                    "Independent audio backing quota was shared with another runtime");
        }
        copy.reset();
        require(!std::filesystem::exists(owned_path) && !budget->bytes && !budget->files,
                "Final audio backing release retained file/quota");
        std::stop_source cancellation;
        cancellation.request_stop();
        rejects([&] {
            AudioStreamFile::create(encode_audio_clip(source), budget, cancellation.get_token());
        });
        require(!budget->bytes && !budget->files, "Cancelled backing changed its quota");
        auto invalid = source;
        invalid.rate = 24000;
        rejects([&] { AudioStreamFile::create(encode_audio_clip(invalid), budget); });
        invalid = source;
        invalid.frames = 16;
        invalid.pcm.resize(invalid.frames * invalid.channels);
        rejects([&] { AudioStreamFile::create(encode_audio_clip(invalid), budget); });
        budget->files = max_audio_stream_backings;
        rejects([&] { AudioStreamFile::create(encode_audio_clip(source), budget); });
        require(budget->files == max_audio_stream_backings && !budget->bytes,
                "Backing count rejection leaked a reservation");
        budget->files = 0;
        for (const auto used :
             {max_audio_stream_backing_bytes, max_audio_stream_backing_bytes + 1}) {
            budget->bytes = used;
            rejects([&] { AudioStreamFile::create(encode_audio_clip(source), budget); });
            require(budget->bytes == used && !budget->files,
                    "Backing byte rejection changed prior accounting");
        }
        budget->bytes = 0;
        AudioRetirement::Sink retained;
        {
            struct Gate {
                std::mutex mutex;
                std::condition_variable changed;
                bool started = false, release = false;
            } gate;
            AudioRetirement retirement;
            retained = retirement.sink();
            struct Release {
                Gate& gate;
                ~Release() {
                    std::lock_guard lock(gate.mutex);
                    gate.release = true;
                    gate.changed.notify_all();
                }
            } release{gate};
            retirement.sink().retire(&gate, [](void* value) noexcept {
                auto& gate = *static_cast<Gate*>(value);
                std::unique_lock lock(gate.mutex);
                gate.started = true;
                gate.changed.notify_all();
                gate.changed.wait(lock, [&] { return gate.release; });
            });
            {
                std::unique_lock lock(gate.mutex);
                gate.changed.wait(lock, [&] { return gate.started; });
            }
            auto delayed_budget = std::make_shared<AudioStreamBudget>(root, retirement.sink());
            auto delayed = AudioStreamFile::create(encode_audio_clip(source), delayed_budget);
            const auto delayed_path = delayed->path();
            delayed.reset();
            require(std::filesystem::exists(delayed_path) && delayed_budget->files == 1 &&
                        retained.pending() >= 2,
                    "Backing release waited for file cleanup on the caller");
            {
                std::lock_guard lock(gate.mutex);
                gate.release = true;
                gate.changed.notify_all();
            }
            retirement.close();
            require(!std::filesystem::exists(delayed_path) && !delayed_budget->files &&
                        !delayed_budget->bytes && !retained.pending(),
                    "Joined audio retirement retained a file, quota or job");
        }
        bool disposed = false;
        retained.retire(&disposed, [](void* value) noexcept { *static_cast<bool*>(value) = true; });
        require(disposed, "Closed retirement sink lost a later immutable backing release");
        std::cout << "Owned audio PCM/WAV backing, independent lifetime, active/orphan "
                     "maintenance, quotas, cancellation and deferred cleanup passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
