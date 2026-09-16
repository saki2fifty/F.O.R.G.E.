#pragma once
#include <filesystem>
#include <memory>
namespace forge {
// Cooperative single-writer ownership for a project on a local filesystem.
// OS handles release on crash; the marker file is never deleted to break a lock.
class ProjectLease {
  public:
    explicit ProjectLease(const std::filesystem::path& root);
    ~ProjectLease();
    ProjectLease(const ProjectLease&) = delete;
    ProjectLease& operator=(const ProjectLease&) = delete;
    void check() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace forge
