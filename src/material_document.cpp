#include "material_document.hpp"
#include "asset_bytes.hpp"
#include <forge/assets.hpp>
#include <forge/scene.hpp>
namespace forge {
namespace {
std::string read(const std::filesystem::path& path) {
    const auto bytes = asset_detail::read_bytes(path, material_source_byte_limit);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace
std::unique_ptr<MaterialDocument>
MaterialDocument::create(std::shared_ptr<const ProjectLease> lease, std::filesystem::path source) {
    if (!lease)
        throw std::runtime_error("Material creation requires project writer ownership");
    lease->check();
    const ProjectPaths paths(lease->root());
    source = ProjectPaths::normalize(source);
    if (!path_utf8(source).ends_with(".material.json"))
        throw std::runtime_error("Material source requires the .material.json suffix");
    const auto file = paths.resolve(source);
    if (std::filesystem::exists(file))
        throw std::runtime_error("Material source already exists");
    const auto catalog = AssetCatalog::open_project(paths.root());
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        if (!record.subasset && paths.same_locator(record.source, source))
            throw std::runtime_error("Material path is already registered to an asset");
    }
    const auto id = AssetId::generate();
    if (catalog.records().contains(id))
        throw std::runtime_error("New material UUID already exists");
    // Single source creation, not catalog publication. A later import failure
    // retains this authored file for correction; never silently deletes it.
    atomic_write(file, MaterialSource::create(id).document.dump(2) + '\n');
    return std::make_unique<MaterialDocument>(std::move(lease), std::move(source));
}
MaterialDocument::MaterialDocument(std::shared_ptr<const ProjectLease> lease,
                                   std::filesystem::path source)
    : lease_(std::move(lease)), owner_(std::this_thread::get_id()) {
    if (!lease_)
        throw std::runtime_error("Material editing requires project writer ownership");
    lease_->check();
    project_ = lease_->root();
    locator_ = ProjectPaths::normalize(source);
    disk_ = read(ProjectPaths(project_).resolve(locator_));
    current_ = MaterialSource::parse(std::as_bytes(std::span(disk_)));
    saved_ = current_.document;
    check_identity();
}
void MaterialDocument::check() const {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("Material source mutation requires its owning thread");
    lease_->check();
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Material document revision exhausted");
}
void MaterialDocument::check_identity() const {
    const ProjectPaths paths(project_);
    const auto catalog = AssetCatalog::open_project(project_);
    for (const auto& [id, record] : catalog.records()) {
        if (id == current_.asset() && (record.subasset || record.type != MaterialAsset::type ||
                                       !paths.same_locator(record.source, locator_)))
            throw std::runtime_error("Material source identity belongs to another asset");
        if (!record.subasset && paths.same_locator(record.source, locator_) &&
            id != current_.asset())
            throw std::runtime_error("Material source path belongs to another identity");
    }
}
void MaterialDocument::bound(std::vector<Entry>& entries) {
    std::size_t bytes = 0;
    for (const auto& e : entries)
        bytes += e.bytes;
    while (entries.size() > 64 || bytes > 8 * 1024 * 1024) {
        bytes -= entries.front().bytes;
        entries.erase(entries.begin());
    }
}
void MaterialDocument::edit(std::uint64_t expected, std::string label,
                            const std::function<void(nlohmann::json&)>& mutation) {
    check();
    if (expected != revision_)
        throw std::runtime_error("Material source revision changed");
    if (!mutation || label.empty() || label.size() > 256)
        throw std::runtime_error("Invalid material edit operation");
    auto candidate = current_;
    mutation(candidate.document);
    candidate.validate();
    if (candidate.asset() != current_.asset())
        throw std::runtime_error("Material edit cannot change persistent identity");
    if (candidate.document == current_.document)
        return;
    undo_.push_back({current_, std::move(label), current_.document.dump().size()});
    bound(undo_);
    redo_.clear();
    current_ = std::move(candidate);
    ++revision_;
}
void MaterialDocument::history(std::vector<Entry>& from, std::vector<Entry>& to) {
    check();
    if (from.empty())
        return;
    // Prepare all allocations before changing the active source/history.
    auto next = from.back().source;
    to.push_back({current_, from.back().label, current_.document.dump().size()});
    bound(to);
    from.pop_back();
    current_ = std::move(next);
    ++revision_;
}
void MaterialDocument::undo() { history(undo_, redo_); }
void MaterialDocument::redo() { history(redo_, undo_); }
void MaterialDocument::save() {
    check();
    check_identity();
    current_.validate();
    const auto file = ProjectPaths(project_).resolve(locator_);
    if (read(file) != disk_)
        throw std::runtime_error(
            "Material source changed on disk; preserve this draft and reopen before overwriting");
    if (!dirty())
        return;
    auto next_saved = current_.document;
    auto next_disk = next_saved.dump(2) + '\n';
    if (next_disk.size() > material_source_byte_limit)
        throw std::runtime_error("Formatted material source exceeds byte limit");
    atomic_write(file, next_disk);
    saved_.swap(next_saved);
    disk_.swap(next_disk);
}
} // namespace forge
