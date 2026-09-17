#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
namespace forge::animation_detail {
// Coupled to Ozz 0.17.0 / 744eb9d99f606eda849acb0b1204f7a3dc20bca1.
// Internal admission API; deliberately contains no Ozz types.
inline constexpr std::size_t max_archive_bytes = 16 * 1024 * 1024;
inline constexpr std::uint32_t max_joints = 1024, max_keys = 262144;
enum class ArchiveKind { Skeleton, Animation };
struct ArchiveInfo {
    std::uint32_t tracks = 0;
    float duration = 0;
    std::vector<std::int16_t> parents;
};
class ArchiveError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
// No Ozz object is constructed or deserialized by this function.
ArchiveInfo validate_archive(std::span<const std::byte>, ArchiveKind);
} // namespace forge::animation_detail
