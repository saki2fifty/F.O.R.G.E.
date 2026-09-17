#pragma once
#include <filesystem>
#include <string>
namespace forge {
std::string path_utf8(const std::filesystem::path& path);
class ProjectPaths {
  public:
    explicit ProjectPaths(std::filesystem::path root);
    const std::filesystem::path& root() const { return root_; }
    static std::filesystem::path normalize(const std::filesystem::path& locator);
    std::filesystem::path resolve(const std::filesystem::path& locator) const;
    std::filesystem::path relative(const std::filesystem::path& absolute) const;
    bool same_locator(const std::filesystem::path& a, const std::filesystem::path& b) const;
    std::filesystem::path assets() const { return resolve("Assets"); }
    std::filesystem::path saved() const { return resolve(".forge/recovery"); }
    std::filesystem::path cache() const { return resolve(".forge/cache"); }
    std::filesystem::path build() const { return resolve(".forge/native"); }

  private:
    std::filesystem::path root_;
};
} // namespace forge
