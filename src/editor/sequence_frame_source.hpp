#pragma once
#include <cstdint>
#include <forge/assets.hpp>
#include <forge/identity.hpp>
#include <memory>
#include <nlohmann/json.hpp>
namespace forge::ui {
struct SequenceFrameSourceInfo {
    EntityRef controller;
    double duration = 0;
    std::uint64_t revision = 0, preview_generation = 0;
    std::shared_ptr<const AssetCatalog> catalog;
};
// Borrowed editor boundary. Timeline retains its preview world, cursor and clock.
// Image export and its minimal GPU tests need no animation-runtime link dependency.
class SequenceFrameSource {
  public:
    virtual ~SequenceFrameSource() = default;
    virtual bool frame_export_ready() const = 0;
    virtual SequenceFrameSourceInfo frame_export_info() const = 0;
    virtual nlohmann::json frame_export_document() const = 0;
    virtual std::uint64_t begin_frame_export() = 0;
    virtual bool frame_export_current(std::uint64_t token) const = 0;
    virtual void sample_frame_export(std::uint64_t token, double seconds) = 0;
    virtual void end_frame_export(std::uint64_t token) = 0;
};
} // namespace forge::ui
