#include "animation_retarget.hpp"
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

namespace forge::animation_detail {
std::vector<std::byte> build_sampled_animation(const SampledAnimation& samples,
                                               std::stop_token stop) {
    const auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    require(samples.times.size() >= 2 && samples.times.size() <= 4096 &&
                samples.poses.size() == samples.times.size() && samples.times.front() == 0,
            "Retarget baking needs 2 to 4096 complete samples starting at zero");
    const auto count = samples.poses.front().size();
    require(count && count <= animation_retarget_joint_limit &&
                samples.times.size() <= animation_retarget_sample_limit / count,
            "Retarget baking exceeds its sampled-joint budget");
    const float duration = samples.times.back();
    require(std::isfinite(duration) && duration >= .0001f && duration <= 60,
            "Retarget baking supports durations from 0.0001 to 60 seconds");
    ozz::animation::offline::RawAnimation raw;
    raw.duration = duration;
    raw.name = "forge_retarget";
    raw.tracks.resize(count);
    for (auto& track : raw.tracks) {
        track.translations.reserve(samples.times.size());
        track.rotations.reserve(samples.times.size());
        track.scales.reserve(samples.times.size());
    }
    for (std::size_t frame = 0; frame < samples.times.size(); ++frame) {
        require(!stop.stop_requested(), "Animation retarget baking cancelled");
        const float time = samples.times[frame];
        require(std::isfinite(time) && time >= 0 && time <= duration &&
                    (!frame || double(time - samples.times[frame - 1]) / duration >= 1e-7),
                "Retarget sample times must increase within the clip duration");
        require(samples.poses[frame].size() == count, "Retarget sampled pose joint counts differ");
        for (std::size_t joint = 0; joint < count; ++joint) {
            const auto& pose = samples.poses[frame][joint];
            for (const auto value : pose.translation)
                require(std::isfinite(value) && std::abs(value) <= 65504,
                        "Retarget translation exceeds the compressed finite channel range");
            for (const auto value : pose.scale)
                require(std::isfinite(value) && std::abs(value - 1) <= 1e-5,
                        "Retarget baking requires unit scale");
            double length = 0;
            for (const auto value : pose.rotation)
                length += double(value) * value;
            require(std::isfinite(length) && std::abs(length - 1) <= 1e-3,
                    "Retarget baking requires finite unit rotations");
            const auto& p = pose.translation;
            const auto& q = pose.rotation;
            const auto& s = pose.scale;
            auto& track = raw.tracks[joint];
            track.translations.push_back({time, {p[0], p[1], p[2]}});
            track.rotations.push_back({time, {q[0], q[1], q[2], q[3]}});
            track.scales.push_back({time, {s[0], s[1], s[2]}});
        }
    }
    require(raw.Validate(), "Retarget samples did not form a valid raw animation");
    ozz::animation::offline::AnimationBuilder builder;
    builder.iframe_interval = 0;
    const auto animation = builder(raw);
    require(bool(animation) && !stop.stop_requested(),
            "Retarget animation build failed or was cancelled");
    ozz::io::MemoryStream stream;
    {
        ozz::io::OArchive archive(&stream, ozz::kLittleEndian);
        archive << *animation;
    }
    require(stream.Size() <= max_archive_bytes, "Retarget archive exceeds its byte limit");
    std::vector<std::byte> result(stream.Size());
    require(stream.Seek(0, ozz::io::Stream::kSet) == 0 &&
                stream.Read(result.data(), result.size()) == result.size(),
            "Retarget archive serialization failed");
    const auto admitted = validate_archive(result, ArchiveKind::Animation);
    require(admitted.tracks == count && admitted.duration == duration && !stop.stop_requested(),
            "Retarget archive differs from its sampled target or was cancelled");
    return result;
}
} // namespace forge::animation_detail
