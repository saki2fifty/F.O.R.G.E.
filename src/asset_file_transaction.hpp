#pragma once
#include <forge/project_lease.hpp>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>
namespace forge {
// Private source-file coordination, not scene Undo or a public plugin protocol.
struct AssetFileChange {
    std::filesystem::path source;
    std::shared_ptr<const std::string> before, after;
};
struct AssetFileCommit {
    std::string transaction;
    std::filesystem::path retained_files;
    std::string cleanup_diagnostic;
};
class AssetFileTransaction {
  public:
    explicit AssetFileTransaction(const ProjectLease&);
    // Sources/sidecars precede exactly one final forge.assets.json commit point.
    // Caller prepares typed domain edits and runs IO on its asset-operation worker.
    AssetFileCommit commit(std::vector<AssetFileChange>, bool retain_backups, std::stop_token = {});
    // Rejects external conflicts without overwriting them. Idempotent.
    bool recover();
    static std::filesystem::path journal();

  private:
    const ProjectLease& lease_;
    std::thread::id owner_ = std::this_thread::get_id();
    void check() const;
};
} // namespace forge
