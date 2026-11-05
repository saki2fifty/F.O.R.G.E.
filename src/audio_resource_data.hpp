#pragma once
#include "audio_stream_file.hpp"
#include <stdexcept>
#include <variant>

namespace forge::asset_detail {
// One immutable selected clip realization. Resident PCM and independently owned
// stream backing share the existing AudioClip resource identity and leases.
class AudioClipResource {
    std::variant<AudioClipData, std::shared_ptr<const AudioStreamFile>> value_;

  public:
    explicit AudioClipResource(AudioClipData clip) : value_(std::move(clip)) {
        validate_audio_clip(std::get<AudioClipData>(value_));
    }
    explicit AudioClipResource(std::shared_ptr<const AudioStreamFile> file)
        : value_(std::move(file)) {
        if (!std::get<std::shared_ptr<const AudioStreamFile>>(value_))
            throw std::runtime_error("Audio stream backing is empty");
    }
    AudioClipInfo info() const {
        if (const auto* clip = std::get_if<AudioClipData>(&value_))
            return {clip->frames, clip->channels, clip->rate};
        return std::get<std::shared_ptr<const AudioStreamFile>>(value_)->info();
    }
    const AudioClipData* resident() const { return std::get_if<AudioClipData>(&value_); }
    std::shared_ptr<const AudioStreamFile> stream_backing() const {
        if (const auto* file = std::get_if<std::shared_ptr<const AudioStreamFile>>(&value_))
            return *file;
        return {};
    }
    std::uint64_t resident_bytes() const {
        const auto* clip = resident();
        return clip ? clip->pcm.capacity() * sizeof(float) : 0;
    }
};
} // namespace forge::asset_detail
