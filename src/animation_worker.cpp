#include "animation_worker.hpp"
#include "asset_worker.hpp"
namespace forge::animation_detail {
void run_converter(const std::filesystem::path& executable, const std::filesystem::path& staging,
                   std::stop_token cancel) {
    asset_detail::run_worker(asset_detail::WorkerKind::Animation, executable, staging, cancel);
}
} // namespace forge::animation_detail
