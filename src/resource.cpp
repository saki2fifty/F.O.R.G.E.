#include <forge/resource.hpp>
#include <limits>
namespace forge {
const char* resource_state_name(ResourceState state) {
    switch (state) {
    case ResourceState::Unloaded:
        return "unloaded";
    case ResourceState::Queued:
        return "queued";
    case ResourceState::DependencyPending:
        return "dependency pending";
    case ResourceState::Loading:
        return "loading";
    case ResourceState::Ready:
        return "ready";
    case ResourceState::Failed:
        return "failed";
    case ResourceState::DependencyFailed:
        return "dependency failed";
    case ResourceState::Cancelled:
        return "cancelled";
    case ResourceState::Stale:
        return "stale";
    case ResourceState::Replacing:
        return "replacing";
    case ResourceState::Retiring:
        return "retiring";
    }
    return "invalid";
}
namespace {
void add(std::uint64_t& a, std::uint64_t b) {
    if (b > UINT64_MAX - a)
        throw std::runtime_error("Resource memory count overflow");
    a += b;
}
} // namespace
std::uint64_t ResourceMemory::total() const {
    std::uint64_t n = 0;
    for (auto v : {cpu_asset, gpu_texture, gpu_buffer, shader_pipeline, animation})
        add(n, v);
    return n;
}
ResourceMemory& ResourceMemory::operator+=(const ResourceMemory& other) {
    auto copy = *this;
    add(copy.cpu_asset, other.cpu_asset);
    add(copy.gpu_texture, other.gpu_texture);
    add(copy.gpu_buffer, other.gpu_buffer);
    add(copy.shader_pipeline, other.shader_pipeline);
    add(copy.animation, other.animation);
    (void)copy.total();
    *this = copy;
    return *this;
}
ResourceInfo ResourceTicket::inspect() const {
    if (!state_)
        throw std::runtime_error("Empty resource request");
    std::lock_guard lock(state_->mutex);
    return state_->info;
}
namespace resource_detail {
void Scope::check() const {
    if (!alive.load() || thread != std::this_thread::get_id())
        throw std::runtime_error("Resource access requires its live owner thread");
}
std::array<std::uint8_t, 16> next_scope() {
    // Ephemeral scope token, never an AssetId or a serialized runtime handle.
    // UUID generation also remains distinct across native module boundaries.
    return detail::uuid_v4();
}
bool terminal(ResourceState state) {
    return state == ResourceState::Unloaded || state == ResourceState::Ready ||
           state == ResourceState::Failed || state == ResourceState::DependencyFailed ||
           state == ResourceState::Cancelled || state == ResourceState::Stale ||
           state == ResourceState::Retiring;
}
void valid_revision(std::string_view value) {
    if (value.size() != 64 || !std::all_of(value.begin(), value.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        throw std::runtime_error("Resource revision must be a lowercase content digest");
}
} // namespace resource_detail
} // namespace forge
