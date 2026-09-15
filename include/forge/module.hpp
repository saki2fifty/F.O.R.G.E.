#pragma once
#include <filesystem>
#include <forge/module_api.h>
#include <string>
namespace forge {
class Module {
  public:
    Module() = default;
    ~Module();
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    void load(const std::filesystem::path& path);
    void tick(const ForgeHostV1& host, float seconds) const;
    const std::string& id() const { return id_; }

  private:
    void* library_{};
    const ForgeModuleV1* api_{};
    std::string id_, schema_;
};
} // namespace forge
