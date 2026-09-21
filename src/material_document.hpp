#pragma once
#include <forge/material_source.hpp>
#include <forge/project_lease.hpp>
#include <functional>
#include <thread>
namespace forge {
// UI-independent, single-document source/history owner. Publication is separate.
class MaterialDocument {
  public:
    MaterialDocument(std::shared_ptr<const ProjectLease>, std::filesystem::path source);
    static std::unique_ptr<MaterialDocument> create(std::shared_ptr<const ProjectLease>,
                                                    std::filesystem::path source);
    const MaterialSource& source() const { return current_; }
    const std::filesystem::path& locator() const { return locator_; }
    const std::filesystem::path& project() const { return project_; }
    std::uint64_t revision() const { return revision_; }
    bool dirty() const { return current_.document != saved_; }
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    void edit(std::uint64_t expected, std::string label,
              const std::function<void(nlohmann::json&)>& mutation);
    void undo();
    void redo();
    // Reject a changed on-disk baseline. Does not publish, import, or edit a scene.
    void save();

  private:
    struct Entry {
        MaterialSource source;
        std::string label;
        std::size_t bytes;
    };
    std::shared_ptr<const ProjectLease> lease_;
    std::filesystem::path project_, locator_;
    std::thread::id owner_;
    MaterialSource current_;
    nlohmann::json saved_;
    std::string disk_;
    std::uint64_t revision_ = 1;
    std::vector<Entry> undo_, redo_;
    void check() const;
    void check_identity() const;
    void history(std::vector<Entry>& from, std::vector<Entry>& to);
    static void bound(std::vector<Entry>&);
};
} // namespace forge
