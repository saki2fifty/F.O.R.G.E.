#include "animation_archive.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string_view>
namespace forge::animation_detail {
namespace {
[[noreturn]] void reject(const char* why) { throw ArchiveError(why); }
class Reader {
  public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {
        if (bytes.size() > max_archive_bytes)
            reject("Animation archive exceeds 16 MiB admission limit");
    }
    std::span<const std::byte> take(std::size_t count) {
        if (count > bytes_.size() - position_)
            reject("Truncated animation archive");
        auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }
    std::uint32_t integer(unsigned size) {
        std::uint32_t value = 0;
        auto bytes = take(size);
        for (unsigned i = 0; i < size; ++i)
            value |= std::to_integer<std::uint32_t>(bytes[i]) << (i * 8);
        return value;
    }
    float real() {
        auto value = std::bit_cast<float>(integer(4));
        if (!std::isfinite(value))
            reject("Non-finite animation value");
        return value;
    }
    void header(std::string_view tag, std::uint32_t version) {
        if (integer(1) != 1)
            reject("Only little-endian Ozz archives are supported");
        for (char c : tag)
            if (integer(1) != static_cast<unsigned char>(c))
                reject("Unsupported Ozz archive type");
        if (integer(1) != 0 || integer(4) != version)
            reject("Unsupported Ozz archive version");
    }
    void end() const {
        if (position_ != bytes_.size())
            reject("Trailing animation archive data");
    }

  private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};
std::uint32_t bounded(Reader& reader, std::uint32_t low, std::uint32_t high) {
    auto n = reader.integer(4);
    if (n < low || n > high)
        reject("Animation archive count exceeds supported bounds");
    return n;
}
ArchiveInfo skeleton(Reader& r) {
    r.header("ozz-skeleton", 2);
    ArchiveInfo info;
    info.tracks = bounded(r, 1, max_joints);
    const auto chars = bounded(r, info.tracks, info.tracks * 256);
    const auto names = r.take(chars);
    std::size_t at = 0;
    for (unsigned i = 0; i < info.tracks; ++i) {
        const auto start = at;
        while (at < names.size() && names[at] != std::byte{0})
            ++at;
        if (at == names.size() || at - start > 255 || at == start)
            reject("Invalid or unterminated skeleton joint name");
        ++at;
    }
    if (at != names.size())
        reject("Unexpected skeleton name data");
    std::vector<std::int16_t> ancestry;
    for (unsigned i = 0; i < info.tracks; ++i) {
        const auto p = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(r.integer(2)));
        if (p < -1 || p >= static_cast<int>(i))
            reject("Invalid skeleton parent index or parent ordering");
        while (!ancestry.empty() && ancestry.back() != p)
            ancestry.pop_back();
        if (p != -1 && ancestry.empty())
            reject("Skeleton hierarchy is not depth-first ordered");
        info.parents.push_back(p);
        ancestry.push_back(static_cast<std::int16_t>(i));
    }
    for (unsigned block = 0; block < (info.tracks + 3) / 4; ++block) {
        std::array<float, 40> values{};
        for (auto& value : values) {
            value = r.real();
            if (std::abs(value) > 65504)
                reject("Skeleton rest transform exceeds supported numeric bounds");
        }
        for (unsigned lane = 0; lane < 4; ++lane) {
            double norm = 0;
            for (unsigned c = 3; c < 7; ++c)
                norm += double(values[c * 4 + lane]) * values[c * 4 + lane];
            if (std::abs(norm - 1) > .002)
                reject("Skeleton rest rotation must be normalized");
        }
    }
    return info;
}
void channel(Reader& r, const std::vector<float>& times, unsigned tracks, unsigned count,
             bool rotation) {
    // All count-derived allocations are bounded before entering this function.
    std::vector<std::uint16_t> ratios(count), previous(count);
    for (auto& index : ratios) {
        index = static_cast<std::uint16_t>(r.integer(times.size() <= 255 ? 1 : 2));
        if (index >= times.size())
            reject("Animation time index is out of range");
    }
    for (auto& offset : previous)
        offset = static_cast<std::uint16_t>(r.integer(2));
    // Disabled iframe generation serializes the builder's default interval 1.
    if (r.real() != 1)
        reject("Unsupported animation seek interval");
    std::vector<unsigned> owner(count), tails(tracks);
    std::vector<std::array<double, 4>> last_rotation(rotation ? tracks : 0);
    float last_previous_time = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (i < tracks) {
            if (previous[i] != 0 || ratios[i] != 0)
                reject("Invalid initial animation keys");
            owner[i] = i;
            tails[i] = i;
        } else {
            if (!previous[i] || previous[i] > i)
                reject("Invalid animation previous-key offset");
            const auto before = i - previous[i];
            const auto track = owner[before];
            if (tails[track] != before || ratios[i] <= ratios[before])
                reject("Broken animation key chain");
            if (i < 2 * tracks && (before != i - tracks || track != i - tracks))
                reject("Invalid second animation key set");
            const auto previous_time = times[ratios[before]];
            if (previous_time < last_previous_time || times[ratios[i]] - previous_time < 1e-8f)
                reject("Animation key times are unordered or too close");
            last_previous_time = previous_time;
            owner[i] = track;
            tails[track] = i;
        }
        const auto a = r.integer(2), b = r.integer(2), c = r.integer(2);
        if (!rotation) {
            if ((a & 0x7c00) == 0x7c00 || (b & 0x7c00) == 0x7c00 || (c & 0x7c00) == 0x7c00)
                reject("Non-finite compressed animation value");
        } else {
            const auto packed =
                std::uint64_t(a) | (std::uint64_t(b) << 16) | (std::uint64_t(c) << 32);
            double sum = 0;
            std::array<double, 4> q{};
            const auto largest = unsigned(packed & 3);
            unsigned component = 0;
            for (unsigned n = 0; n < 3; ++n) {
                const double v =
                    double((packed >> (3 + n * 15)) & 32767) * (1.4142135623730951 / 32767.0) -
                    .7071067811865476;
                if (component == largest)
                    ++component;
                q[component++] = v;
                sum += v * v;
            }
            if (sum > .7501)
                reject("Invalid compressed animation rotation");
            q[largest] = std::sqrt(1 - sum) * ((packed & 4) ? -1 : 1);
            for (double value : q)
                if (std::abs(value) > std::abs(q[largest]) + .0001)
                    reject("Invalid largest quaternion component");
            if (i >= tracks) {
                double dot = 0;
                for (unsigned n = 0; n < 4; ++n)
                    dot += q[n] * last_rotation[owner[i]][n];
                if (dot < -.0002)
                    reject("Animation rotations do not use supported continuous hemispheres");
            }
            last_rotation[owner[i]] = q;
        }
    }
    for (auto tail : tails)
        if (times[ratios[tail]] != 1)
            reject("Animation track does not end at duration");
}
ArchiveInfo animation(Reader& r) {
    r.header("ozz-animation", 7);
    ArchiveInfo info;
    info.duration = r.real();
    if (info.duration < .0001f || info.duration > 3600)
        reject("Animation duration must be between 0.0001 and 3600 seconds");
    info.tracks = bounded(r, 1, max_joints);
    const auto name = bounded(r, 0, 255);
    const auto time_count = bounded(r, 2, 65535);
    const auto padded = (info.tracks + 3) / 4 * 4;
    const std::array counts{bounded(r, padded * 2, max_keys), bounded(r, padded * 2, max_keys),
                            bounded(r, padded * 2, max_keys)};
    for (unsigned i = 0; i < 6; ++i)
        if (r.integer(4) != 0)
            reject("Animation seek iframes are not supported");
    auto text = r.take(name);
    if (std::find(text.begin(), text.end(), std::byte{0}) != text.end())
        reject("Embedded null in animation name");
    std::vector<float> times(time_count);
    for (unsigned i = 0; i < time_count; ++i) {
        times[i] = r.real();
        if (times[i] < 0 || times[i] > 1 || (i && times[i] - times[i - 1] < 1e-8f))
            reject("Invalid animation time table");
    }
    if (times.front() != 0 || times.back() != 1)
        reject("Animation time table must span zero to one");
    for (unsigned c = 0; c < 3; ++c)
        channel(r, times, padded, counts[c], c == 1);
    return info;
}
} // namespace
ArchiveInfo validate_archive(std::span<const std::byte> bytes, ArchiveKind kind) {
    Reader reader(bytes);
    auto result = kind == ArchiveKind::Skeleton ? skeleton(reader) : animation(reader);
    reader.end();
    return result;
}
} // namespace forge::animation_detail
