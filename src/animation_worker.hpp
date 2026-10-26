#pragma once
#include <filesystem>
#include <stop_token>
namespace forge::asset_detail {
class WorkerStageLease;
}
namespace forge::animation_detail {
// Private, fixed-command official converter worker. No shell and no arbitrary arguments.
void run_converter(const std::filesystem::path& executable, const std::filesystem::path& staging,
                   std::stop_token cancel, const asset_detail::WorkerStageLease* owner = nullptr);
} // namespace forge::animation_detail
