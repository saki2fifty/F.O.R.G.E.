#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace forge::asset_detail {
// Private temporary-storage ownership. The designated worker inherits this
// open file description/handle, so parent death alone cannot permit cleanup.
// It is unrelated to logical AssetIds, the SDK and renderer resource leases.
class WorkerStageLease {
  public:
    WorkerStageLease(const std::filesystem::path& file, std::string_view marker);
    ~WorkerStageLease();
    WorkerStageLease(const WorkerStageLease&) = delete;
    WorkerStageLease& operator=(const WorkerStageLease&) = delete;
    // No creation/truncation. Null means a live owner still holds the marker.
    static std::unique_ptr<WorkerStageLease> try_open(const std::filesystem::path& file);
    std::string marker() const;
    std::uintptr_t inheritance_handle() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
    WorkerStageLease();
    bool open(const std::filesystem::path& file, bool create);
};
} // namespace forge::asset_detail
