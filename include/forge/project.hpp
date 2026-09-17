#pragma once
#include <forge/input.hpp>
#include <forge/project_paths.hpp>
#include <optional>
namespace forge {
// Shared project behavior only. No user layout, machine tools or live tick state.
class ProjectSettings {
  public:
    explicit ProjectSettings(std::filesystem::path root);
    static void validate(const nlohmann::json& data);
    const nlohmann::json& document() const { return data_; }
    double simulation_hz() const { return data_.value("simulation_hz", 60.0); }
    InputMap input() const { return InputMap(data_.at("input")); }
    std::optional<std::filesystem::path> startup() const;
    void save(nlohmann::json candidate, const nlohmann::json* expected = nullptr);
    static nlohmann::json defaults(const std::string& name);

  private:
    ProjectPaths paths_;
    nlohmann::json data_;
    std::optional<nlohmann::json> disk_;
};
} // namespace forge
