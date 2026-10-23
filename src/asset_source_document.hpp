#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <atomic>
#include <forge/assets.hpp>
#include <forge/project_lease.hpp>
#include <forge/scene.hpp>
#include <functional>
#include <thread>
#include <type_traits>
#include <utility>
namespace forge {
// Transient routing only: never serialized or used as an asset identity.
inline std::uint64_t next_asset_document_session() {
    static std::atomic<std::uint64_t> next{1};
    const auto value = next.fetch_add(1, std::memory_order_relaxed);
    if (value >= 9007199254740991ULL)
        throw std::runtime_error("Asset document session space exhausted");
    return value;
}
// UI-independent, single-document source/history owner. Publication is separate.
template <class Traits> class AssetSourceDocument {
    using Source = typename Traits::Source;
    using Value = std::remove_cvref_t<decltype(std::declval<Source>().document)>;
    static_assert(std::is_nothrow_swappable_v<Value>,
                  "Asset source saved-value adoption must not throw after file publication");

  public:
    AssetSourceDocument(std::shared_ptr<const ProjectLease>, std::filesystem::path source);
    static std::unique_ptr<AssetSourceDocument>
    create(std::shared_ptr<const ProjectLease>, std::filesystem::path source,
           std::function<Source(AssetId)> initialize = {});
    static constexpr auto source_suffix = Traits::suffix;
    static auto initial_source(AssetId id) { return Traits::create(id); }
    std::uint64_t session() const { return session_; }
    const Source& source() const { return current_; }
    const std::filesystem::path& locator() const { return locator_; }
    const std::filesystem::path& project() const { return project_; }
    std::uint64_t revision() const { return revision_; }
    bool dirty() const { return current_.document != saved_; }
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    // Preflight a gesture without changing source or history. Both directions
    // must fit so the resulting Undo can itself retain a Redo entry.
    bool retains_history(const Source& candidate) const {
        check();
        return history_weight(current_, candidate) <= history_budget &&
               history_weight(candidate, current_) <= history_budget;
    }
    void edit(std::uint64_t expected, std::string label,
              const std::function<void(nlohmann::json&)>& mutation);
    // Same validation/history owner for sources with an immutable typed value.
    void edit_source(std::uint64_t expected, std::string label,
                     const std::function<void(Source&)>& mutation);
    void undo();
    void redo();
    // Reject a changed on-disk baseline. Does not publish, import, or edit a scene.
    void save();

  private:
    struct Entry {
        Source source;
        std::string label;
        std::size_t bytes;
    };
    std::shared_ptr<const ProjectLease> lease_;
    std::filesystem::path project_, locator_;
    std::thread::id owner_;
    Source current_;
    Value saved_;
    std::string disk_;
    const std::uint64_t session_ = next_asset_document_session();
    std::uint64_t revision_ = 1;
    std::vector<Entry> undo_, redo_;
    void check() const;
    void check_identity() const;
    void history(std::vector<Entry>& from, std::vector<Entry>& to);
    static void bound(std::vector<Entry>&);
    static std::size_t history_weight(const Source&, const Source&);
    static constexpr std::size_t history_budget = 8 * 1024 * 1024;
    static std::string read(const std::filesystem::path& path) {
        const auto bytes = asset_detail::read_bytes(path, Traits::byte_limit);
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
};

template <class Traits>
std::unique_ptr<AssetSourceDocument<Traits>>
AssetSourceDocument<Traits>::create(std::shared_ptr<const ProjectLease> lease,
                                    std::filesystem::path source,
                                    std::function<Source(AssetId)> initialize) {
    if (!lease)
        throw std::runtime_error("Asset source creation requires project writer ownership");
    lease->check();
    const ProjectPaths paths(lease->root());
    source = ProjectPaths::normalize(source);
    if (!path_utf8(source).ends_with(Traits::suffix))
        throw std::runtime_error(std::string("Asset source requires the ") + Traits::suffix +
                                 " suffix");
    const auto file = paths.resolve(source);
    if (std::filesystem::exists(file))
        throw std::runtime_error("Asset source already exists");
    const auto catalog = AssetCatalog::open_project(paths.root());
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        if (!record.subasset && paths.same_locator(record.source, source))
            throw std::runtime_error("Asset source path is already registered to an asset");
    }
    const auto id = AssetId::generate();
    if (catalog.records().contains(id))
        throw std::runtime_error("New asset source UUID already exists");
    // Single source creation, not catalog publication. A later import failure
    // retains this authored file for correction; never silently deletes it.
    const auto candidate = initialize ? initialize(id) : Traits::create(id);
    const auto bytes = candidate.document.dump(2) + '\n';
    if (bytes.size() > Traits::byte_limit)
        throw std::runtime_error("New asset source exceeds document byte limit");
    const auto validated = Source::parse(std::as_bytes(std::span(bytes)));
    if (validated.asset() != id)
        throw std::runtime_error("New asset source must retain its allocated identity");
    atomic_write(file, bytes);
    return std::make_unique<AssetSourceDocument<Traits>>(std::move(lease), std::move(source));
}
template <class Traits>
AssetSourceDocument<Traits>::AssetSourceDocument(std::shared_ptr<const ProjectLease> lease,
                                                 std::filesystem::path source)
    : lease_(std::move(lease)), owner_(std::this_thread::get_id()) {
    if (!lease_)
        throw std::runtime_error("Asset source editing requires project writer ownership");
    lease_->check();
    project_ = lease_->root();
    locator_ = ProjectPaths::normalize(source);
    disk_ = read(ProjectPaths(project_).resolve(locator_));
    current_ = Source::parse(std::as_bytes(std::span(disk_)));
    saved_ = current_.document;
    check_identity();
}
template <class Traits> void AssetSourceDocument<Traits>::check() const {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("Asset source mutation requires its owning thread");
    lease_->check();
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Asset source document revision exhausted");
}
template <class Traits> void AssetSourceDocument<Traits>::check_identity() const {
    const ProjectPaths paths(project_);
    const auto catalog = AssetCatalog::open_project(project_);
    for (const auto& [id, record] : catalog.records()) {
        if (id == current_.asset() && (record.subasset || record.type != Traits::asset_type ||
                                       !paths.same_locator(record.source, locator_)))
            throw std::runtime_error("Asset source identity belongs to another asset");
        if (!record.subasset && paths.same_locator(record.source, locator_) &&
            id != current_.asset())
            throw std::runtime_error("Asset source path belongs to another identity");
    }
}
template <class Traits> void AssetSourceDocument<Traits>::bound(std::vector<Entry>& entries) {
    std::size_t bytes = 0;
    for (const auto& e : entries)
        bytes += e.bytes;
    while (entries.size() > 64 || bytes > history_budget) {
        bytes -= entries.front().bytes;
        entries.erase(entries.begin());
    }
}
template <class Traits>
std::size_t AssetSourceDocument<Traits>::history_weight(const Source& from, const Source& to) {
    const std::size_t bytes = [&] {
        if constexpr (requires { Traits::history_weight(from, to); })
            return Traits::history_weight(from, to);
        else
            return from.document.dump().size();
    }();
    if (!bytes)
        throw std::runtime_error("Asset source history weight must be positive");
    // Larger entries are immediately evicted. Saturation also bounds summation
    // when a source policy reports an exhausted/oversized weight.
    return std::min(bytes, history_budget + 1);
}
template <class Traits>
void AssetSourceDocument<Traits>::edit(std::uint64_t expected, std::string label,
                                       const std::function<void(nlohmann::json&)>& mutation) {
    std::function<void(Source&)> update;
    if (mutation)
        update = [&](Source& candidate) { mutation(candidate.document); };
    edit_source(expected, std::move(label), update);
}
template <class Traits>
void AssetSourceDocument<Traits>::edit_source(std::uint64_t expected, std::string label,
                                              const std::function<void(Source&)>& mutation) {
    check();
    if (expected != revision_)
        throw std::runtime_error("Asset source revision changed");
    if (!mutation || label.empty() || label.size() > 256)
        throw std::runtime_error("Invalid asset source edit operation");
    auto candidate = current_;
    mutation(candidate);
    candidate.validate();
    if constexpr (requires { Traits::validate_edit(current_, candidate); })
        Traits::validate_edit(current_, candidate);
    if (candidate.asset() != current_.asset())
        throw std::runtime_error("Asset source edit cannot change persistent identity");
    if (candidate.document == current_.document)
        return;
    const auto weight = history_weight(current_, candidate);
    undo_.push_back({current_, std::move(label), weight});
    bound(undo_);
    redo_.clear();
    current_ = std::move(candidate);
    ++revision_;
}
template <class Traits>
void AssetSourceDocument<Traits>::history(std::vector<Entry>& from, std::vector<Entry>& to) {
    check();
    if (from.empty())
        return;
    // Prepare all allocations before changing the active source/history.
    auto next = from.back().source;
    if constexpr (requires { Traits::restore_identity_metadata(next, current_); })
        Traits::restore_identity_metadata(next, current_);
    const auto weight = history_weight(current_, next);
    to.push_back({current_, from.back().label, weight});
    bound(to);
    from.pop_back();
    current_ = std::move(next);
    ++revision_;
}
template <class Traits> void AssetSourceDocument<Traits>::undo() { history(undo_, redo_); }
template <class Traits> void AssetSourceDocument<Traits>::redo() { history(redo_, undo_); }
template <class Traits> void AssetSourceDocument<Traits>::save() {
    check();
    check_identity();
    current_.validate();
    const auto file = ProjectPaths(project_).resolve(locator_);
    if (read(file) != disk_)
        throw std::runtime_error(
            "Asset source changed on disk; preserve this draft and reopen before overwriting");
    if (!dirty())
        return;
    auto next_saved = current_.document;
    auto next_disk = next_saved.dump(2) + '\n';
    if (next_disk.size() > Traits::byte_limit)
        throw std::runtime_error("Formatted asset source exceeds byte limit");
    atomic_write(file, next_disk);
    using std::swap;
    swap(saved_, next_saved);
    disk_.swap(next_disk);
}
} // namespace forge
