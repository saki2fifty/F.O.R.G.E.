#pragma once
#include "../code_semantic.hpp"
#include "../gameplay_source_identity.hpp"
#include "build_command.hpp"
#include "cpp_source_search.hpp"
#include "event_time.hpp"

namespace forge::ui {
// One derived query owned by C++ Sources. The existing command supervisor owns
// process groups/Jobs; this class owns only its snapshot, cache files and result.
class CppSemanticTask {
    BuildCommand command_;
    std::filesystem::path project_, directory_;
    CppSearchDrafts drafts_;
    std::string operation_, kind_, path_, context_generation_, disk_state_, context_state_, error_;
    nlohmann::json result_;
    TimedTextLog log_;
    bool cancelled_ = false, stale_ = false;

    void cleanup() noexcept {
        if (!directory_.empty() && command_.retired()) {
            try {
                const ProjectPaths paths(project_);
                const auto expected = std::filesystem::path(".forge/native/semantic") / operation_;
                if (paths.same_locator(paths.relative(directory_), expected)) {
                    std::error_code ignored;
                    std::filesystem::remove_all(directory_, ignored);
                }
            } catch (...) {
            }
        }
        directory_.clear();
    }

  public:
    ~CppSemanticTask() { close(); }
    bool busy() const { return command_.running(); }
    bool stale() const { return stale_; }
    const std::string& operation() const { return operation_; }
    const std::string& error() const { return error_; }
    const nlohmann::json& result() const { return result_; }
    const std::filesystem::path& project() const { return project_; }
    const std::string& log() const { return log_.text(); }
    void close() noexcept {
        command_.close();
        cleanup();
        operation_.clear();
        drafts_.clear();
        result_ = nullptr;
        project_.clear();
        kind_.clear();
        path_.clear();
        context_generation_.clear();
        disk_state_.clear();
        context_state_.clear();
        error_.clear();
        log_.clear();
        cancelled_ = stale_ = false;
    }
    void invalidate() {
        if (!operation_.empty())
            stale_ = true;
    }
    void observe_generation(std::string_view generation) {
        if (!operation_.empty() && generation != context_generation_)
            stale_ = true;
    }
    void observe(std::string_view path, const std::string& text) {
        if (operation_.empty())
            return;
        const auto found = drafts_.find(std::filesystem::u8path(path));
        if (found == drafts_.end() || found->second != text)
            stale_ = true;
    }
    void cancel(const std::string& operation) {
        if (operation != operation_ || operation.empty())
            throw std::runtime_error("This is not the current C++ analysis operation");
        if (cancelled_)
            return;
        cancelled_ = true;
        result_ = nullptr;
        if (busy())
            command_.cancel();
        else
            log_.append("C++ analysis cancelled\n");
    }
    void start(const CppSemanticContext& context, const std::string& tool, const std::string& path,
               const std::string& kind, std::size_t offset, CppSearchDrafts drafts) {
        if (busy())
            throw std::runtime_error("Wait for C++ analysis or cancel the current query first");
        if (!cpp_semantic_kind(kind))
            throw std::runtime_error("Choose a supported C++ analysis kind");
        const auto source = drafts.find(std::filesystem::u8path(path));
        if (source == drafts.end() || offset > source->second.size() || drafts.size() > 64)
            throw std::runtime_error("Choose an open source position, with at most 64 open tabs");
        // Validate position before any tool launch. Full protocol validation is
        // repeated by the isolated worker at its own trust boundary.
        if (offset < source->second.size() &&
            (static_cast<unsigned char>(source->second[offset]) & 0xc0) == 0x80)
            throw std::runtime_error("C++ analysis cursor splits a UTF-8 character");
        close();
        try {
            project_ = context.project;
            context_generation_ = context.generation;
            disk_state_ = gameplay_source_digest(project_);
            const auto database =
                asset_storage::read(context.directory / "compile_commands.json", 16 * 1024 * 1024);
            if (!database)
                throw std::runtime_error("C++ analysis compiler context is unavailable");
            context_state_ = cpp_source_state(*database);
            operation_ = AssetId::generate().str();
            kind_ = kind;
            path_ = path;
            drafts_ = std::move(drafts);
            cancelled_ = stale_ = false;
            error_.clear();
            log_.clear();
            log_.append("C++ analysis started\n");
            const ProjectPaths paths(project_);
            const auto expected = std::filesystem::path(".forge/native/semantic") / operation_;
            directory_ = paths.resolve(expected);
            if (!paths.same_locator(paths.relative(directory_), expected))
                throw std::runtime_error("C++ analysis cache redirects into another project owner");
            asset_storage::ordinary(directory_);
            std::filesystem::create_directories(directory_);
            nlohmann::json documents = nlohmann::json::array();
            for (const auto& [locator, text] : drafts_)
                documents.push_back({{"path", path_utf8(locator)}, {"text", text}});
            const auto request = nlohmann::json{{"version", 1},
                                                {"id", operation_},
                                                {"project", path_utf8(project_)},
                                                {"context", path_utf8(context.directory)},
                                                {"tool", tool},
                                                {"kind", kind_},
                                                {"path", path_},
                                                {"offset", offset},
                                                {"documents", std::move(documents)}}
                                     .dump();
            if (request.size() > 64 * 1024 * 1024)
                throw std::runtime_error("C++ analysis request exceeds its byte budget");
            asset_storage::replace(directory_ / "request.json", request);
            auto helper = self_executable().parent_path() / "forge_tools";
#ifdef _WIN32
            helper += ".exe";
#endif
            command_.start({path_utf8(helper), "--cpp-semantic-worker",
                            path_utf8(directory_ / "request.json"),
                            path_utf8(directory_ / "response.json")},
                           context.environment, project_, 45000);
        } catch (const std::exception& error) {
            error_ = error.what();
            log_.append(error_ + '\n');
            cleanup();
            throw;
        }
    }
    void poll() {
        if (!busy())
            return;
        try {
            std::string output;
            int code = 0;
            const bool finished = command_.pump(output, code);
            log_.append(output);
            if (!finished)
                return;
            if (cancelled_) {
                log_.append("C++ analysis cancelled\n");
                return;
            }
            if (code || !command_.retired())
                throw std::runtime_error("C++ analysis failed; see its timestamped log");
            const auto text = asset_storage::read(directory_ / "response.json", 16 * 1024 * 1024);
            if (!text)
                throw std::runtime_error("C++ analysis response is missing");
            auto result = asset_detail::parse_bounded_json(
                std::as_bytes(std::span(text->data(), text->size())), 16 * 1024 * 1024);
            const auto source = drafts_.find(std::filesystem::u8path(path_));
            if (result.at("version").get<std::int64_t>() != 1 ||
                result.at("id").get<std::string>() != operation_ ||
                result.at("path").get<std::string>() != path_ ||
                result.at("kind").get<std::string>() != kind_ ||
                result.at("state").get<std::string>() != cpp_source_state(source->second) ||
                result.at("context_state").get<std::string>() != context_state_ ||
                !result.at("items").is_array() || result.at("items").size() > 64)
                throw std::runtime_error("C++ analysis returned an invalid result envelope");
            stale_ = stale_ || gameplay_source_digest(project_) != disk_state_;
            if (kind_ == "diagnostics")
                result["occurred_at"] = event_time_local();
            result_ = std::move(result);
            log_.append("C++ analysis completed\n");
        } catch (const std::exception& error) {
            if (!error_.empty())
                return;
            error_ = error.what();
            result_ = nullptr;
            log_.append(error_ + '\n');
            command_.cancel();
        }
    }
    bool current(const CppSemanticContext& context, const CppSearchDrafts& drafts) const {
        if (stale_ || busy() || cancelled_ || !error_.empty() || result_.is_null() ||
            context.project != project_ || context.generation != context_generation_ ||
            drafts != drafts_ || gameplay_source_digest(project_) != disk_state_)
            return false;
        const auto database =
            asset_storage::read(context.directory / "compile_commands.json", 16 * 1024 * 1024);
        return database && cpp_source_state(*database) == context_state_;
    }
    nlohmann::json state() const {
        return {{"operation", operation_},
                {"busy", busy()},
                {"cancelled", cancelled_},
                {"stale", stale_},
                {"error", error_},
                {"kind", kind_},
                {"path", path_},
                {"complete", !result_.is_null()},
                {"total", result_.is_null() ? std::size_t{} : result_.at("items").size()},
                {"limited", !result_.is_null() && result_.at("limited").get<bool>()}};
    }
};
} // namespace forge::ui
