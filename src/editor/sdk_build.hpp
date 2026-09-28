#pragma once
#include "../asset_storage.hpp"
#include "../authored_inspection.hpp"
#include "document.hpp"
#include "native_build.hpp"
#include <future>
namespace forge {
// Stopped-only exact-SDK compiler task. All library admission remains isolated.
// The project manifest is the publication point; failed candidates never replace it.
class SdkBuild {
  public:
    explicit SdkBuild(std::filesystem::path project) : project_(std::move(project)) {}
    ~SdkBuild() { stop_.request_stop(); }
    bool managed() const {
        return std::filesystem::is_regular_file(project_ / "Native/forge.sdk-project.json");
    }
    bool busy() const { return phase_ != Phase::Idle; }
    const std::string& status() const { return status_; }
    const std::string& error() const { return error_; }
    const std::string& log() const { return log_; }
    std::filesystem::path module_kits(const ProjectSettings& settings) const {
        if (!kits_.empty())
            return kits_;
        if (managed())
            for (const auto& module : settings.document().value("modules", Json::array()))
                if (module.is_object() && module.value("id", "") == "project.gameplay") {
                    const auto file = ProjectPaths(project_).resolve(
                        std::filesystem::u8path(module.at("library").get<std::string>()));
                    if (std::filesystem::is_regular_file(file.parent_path() /
                                                         "forge.module-kit.json"))
                        return file.parent_path().parent_path();
                }
        return {};
    }
    void create(SceneDocument& project, const std::filesystem::path& sdk) {
        project.check_ownership();
        check_sdk(sdk);
        const auto source = ProjectPaths(project_).resolve("Native");
        const bool empty = std::filesystem::is_directory(source) &&
                           !std::filesystem::is_symlink(source) &&
                           std::filesystem::is_empty(source);
        if (std::filesystem::exists(std::filesystem::symlink_status(source)) && !empty)
            throw std::runtime_error("Native already contains files; none were overwritten.");
        const auto pending = project_ / ("Native.pending-" + AssetId::generate().str());
        std::filesystem::create_directory(pending);
        try {
            for (const auto* name : {"CMakeLists.txt", "gameplay.cpp"})
                std::filesystem::copy_file(sdk / "sdk/template" / name, pending / name);
            asset_storage::replace(
                pending / "forge.sdk-project.json",
                R"({"format":"forge.sdk-project","version":1,"module":"project.gameplay"})");
            if (empty && !std::filesystem::remove(source))
                throw std::runtime_error("Native changed while creating source.");
            std::filesystem::rename(pending, source);
            status_ = "Source created at Native/gameplay.cpp. Build to register the module.";
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(pending, ignored);
            throw;
        }
    }
    void build(SceneDocument& project, std::filesystem::path sdk, const std::string& cmake,
               const std::string& ninja) {
        if (busy())
            return;
        try {
            project.check_ownership();
            check_sdk(sdk);
            if (!managed())
                throw std::runtime_error(
                    "This project uses an external SDK build. Its sources were not changed.");
            const auto marker = read_json(project_ / "Native/forge.sdk-project.json");
            if (marker.value("format", "") != "forge.sdk-project" || marker.at("version") != 1 ||
                marker.at("module") != "project.gameplay")
                throw std::runtime_error("Unsupported gameplay build recipe.");
            sdk_ = std::filesystem::absolute(sdk);
            cmake_ = cmake;
            expected_ = project.settings().document();
            for (const auto& module : expected_.value("modules", Json::array()))
                if (module.is_object() && module.value("id", "") != "project.gameplay")
                    throw std::runtime_error(
                        "Additional external SDK modules require your external build workflow; "
                        "the managed starter never rewrites their deployment.");
            candidate_ = expected_;
            log_.clear();
            error_.clear();
            work_ = ProjectPaths(project_).resolve(".forge/sdk-build");
            std::filesystem::create_directories(work_);
            log_file_.close();
            log_file_.open(work_ / "build.log", std::ios::trunc);
            if (!log_file_)
                throw std::runtime_error("Cannot open SDK build log.");
            candidate_root_ = work_ / AssetId::generate().str();
            std::filesystem::create_directory(candidate_root_);
            command_.start({cmake_, "-S", (project_ / "Native").string(), "-B",
                            (work_ / "build").string(), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                            "-DForgeNativeSdk_DIR=" + (sdk_ / "sdk").string(),
                            "-DCMAKE_MAKE_PROGRAM=" + ninja});
            phase_ = Phase::Configure;
            status_ = "Configuring with the matching installed SDK.";
        } catch (const std::exception& e) {
            fail(e.what());
            throw;
        }
    }
    void cancel() {
        stop_.request_stop();
        command_.close();
        if (phase_ != Phase::Inspect)
            fail("Build cancelled; the last good module is unchanged.");
    }
    void pump(SceneDocument& project) {
        if (!busy())
            return;
        try {
            project.check_ownership();
            if (project.project() != project_ || project.settings().document() != expected_)
                throw std::runtime_error(
                    "Project settings changed during build; candidate not published.");
            if (phase_ == Phase::Inspect) {
                if (inspection_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return;
                (void)inspection_.get();
                if (stop_.stop_requested())
                    throw std::runtime_error("SDK build cancelled.");
                const auto deployment =
                    ProjectPaths(project_).resolve("Native/Builds") / AssetId::generate().str();
                unpublished_deployment_ = deployment;
                std::filesystem::create_directories(deployment.parent_path());
                // Move the complete validated kit first. The manifest switches only on success.
                std::filesystem::rename(candidate_root_ / "kits", deployment);
                auto modules = candidate_.at("modules");
                for (auto& module : modules)
                    if (module.is_object() && module.at("id") == "project.gameplay")
                        module["library"] =
                            path_utf8(std::filesystem::relative(deployment, project_) /
                                      "project.gameplay" / library_);
                candidate_["modules"] = std::move(modules);
                auto published_kits = deployment;
                std::string completed_status =
                    "Gameplay built and registered. Inspect components, then restart Play.";
                project.save_settings(candidate_, &expected_);
                unpublished_deployment_.clear();
                kits_.swap(published_kits);
                phase_ = Phase::Idle;
                status_.swap(completed_status);
                cleanup();
                return;
            }
            std::string output;
            int code = 0;
            const bool finished = command_.pump(output, code);
            log_file_ << output;
            log_file_.flush();
            log_ += output;
            if (log_.size() > 256 * 1024)
                log_.erase(0, log_.size() - 256 * 1024);
            if (!finished)
                return;
            if (code)
                throw std::runtime_error("SDK build command failed (" + std::to_string(code) +
                                         "). See build output; last good module retained.");
            if (phase_ == Phase::Configure) {
                command_.start({cmake_, "--build", (work_ / "build").string(), "--target",
                                "gameplay", "--parallel", "2"});
                phase_ = Phase::Compile;
                status_ = "Compiling gameplay; previous module retained.";
            } else if (phase_ == Phase::Compile) {
                command_.start({cmake_, "--install", (work_ / "build").string(), "--component",
                                "GameplayRuntime", "--prefix",
                                (candidate_root_ / "kits").string()});
                phase_ = Phase::Install;
                status_ = "Preparing complete runtime deployment kit.";
            } else {
                const auto kit =
                    read_json(candidate_root_ / "kits/project.gameplay/forge.module-kit.json");
                if (kit.at("format") != "forge.module-kit" || kit.at("version") != 1)
                    throw std::runtime_error("Unsupported gameplay deployment kit.");
                library_ = kit.at("library").get<std::string>();
                const auto fingerprint = kit.at("fingerprint").get<std::string>();
                auto modules = candidate_.value("modules", Json::array());
                for (auto it = modules.begin(); it != modules.end();) {
                    if (it->is_object() && it->value("id", "") == "project.gameplay")
                        it = modules.erase(it);
                    else
                        ++it;
                }
                modules.push_back({{"id", "project.gameplay"},
                                   {"sdk", "experimental-1"},
                                   {"implementation", "1"},
                                   {"fingerprint", fingerprint},
                                   {"library", "kits/project.gameplay/" + library_},
                                   {"dependencies", Json::array()}});
                candidate_["modules"] = std::move(modules);
                ProjectSettings::validate(candidate_);
                asset_storage::replace(candidate_root_ / "forge.project.json", candidate_.dump(2));
                stop_ = std::stop_source{};
                const auto runtime = sdk_ / "bin/forge_runtime.exe";
                const auto root = candidate_root_;
                const auto stop = stop_.get_token();
                inspection_ = std::async(std::launch::async, [runtime, root, fingerprint, stop] {
                    return detail::inspect_project_authoring(runtime, root, fingerprint, stop);
                });
                phase_ = Phase::Inspect;
                status_ = "Validating gameplay in an isolated runtime.";
            }
        } catch (const std::exception& e) {
            fail(e.what());
        }
    }

  private:
    enum class Phase { Idle, Configure, Compile, Install, Inspect };
    Phase phase_ = Phase::Idle;
    std::filesystem::path project_, sdk_, work_, candidate_root_, kits_, unpublished_deployment_;
    BuildCommand command_;
    Json expected_, candidate_;
    std::future<Json> inspection_;
    std::stop_source stop_;
    std::ofstream log_file_;
    std::string cmake_, library_, status_ = "Create an exact-SDK gameplay source project.", error_,
                                  log_;
    static void check_sdk(const std::filesystem::path& sdk) {
        const auto info = read_json(sdk / "build.json");
        if (info.value("linkage_profile", "") != "shared-native-sdk" ||
            info.value("source_commit", "") != forge::source_commit)
            throw std::runtime_error("Choose the NativeSdk shipped with this editor build.");
        if (!std::filesystem::is_regular_file(sdk / "sdk/ForgeNativeSdkConfig.cmake") ||
            !std::filesystem::is_regular_file(sdk / "bin/forge_runtime.exe"))
            throw std::runtime_error("Native SDK installation is incomplete.");
    }
    void cleanup() {
        if (candidate_root_.empty())
            return;
        std::error_code ignored;
        std::filesystem::remove_all(candidate_root_, ignored);
        candidate_root_.clear();
    }
    void fail(const std::string& error) {
        stop_.request_stop();
        command_.close();
        error_ = error;
        if (inspection_.valid()) {
            if (inspection_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            try {
                (void)inspection_.get();
            } catch (...) {
            }
        }
        cleanup();
        if (!unpublished_deployment_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(unpublished_deployment_, ignored);
            unpublished_deployment_.clear();
        }
        phase_ = Phase::Idle;
        status_ = "Build failed; the last good module and project settings remain active.";
    }
};
} // namespace forge
