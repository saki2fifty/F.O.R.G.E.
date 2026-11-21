#pragma once
#include "bounded_json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace forge {
// A bounded observation on the graphical host's existing supervised stdout.
// The presentation and profiling services retain all query/sample ownership.
inline constexpr std::string_view standalone_gpu_profile_prefix = "FORGE GPU PROFILE ";
inline constexpr std::size_t standalone_gpu_profile_bytes = 1024 * 1024;
inline constexpr unsigned standalone_gpu_profile_version = 1;
inline constexpr std::string_view standalone_gpu_profile_live_prefix = "FORGE GPU LIVE ";
inline constexpr unsigned standalone_gpu_profile_live_version = 1;
inline bool standalone_gpu_profile_version_matches(const nlohmann::json& value, unsigned expected) {
    return value.is_number_unsigned()
               ? value.get<std::uint64_t>() == expected
               : value.is_number_integer() && value.get<std::int64_t>() == expected;
}
inline bool standalone_gpu_profile_supported(const nlohmann::json& engine, bool live = false) {
    if (!engine.is_object())
        return false;
    if (live && !standalone_gpu_profile_supported(engine))
        return false;
    const auto support = engine.find(live ? "runtime_gpu_profiling_live" : "runtime_gpu_profiling");
    return support != engine.end() && standalone_gpu_profile_version_matches(
                                          *support, live ? standalone_gpu_profile_live_version
                                                         : standalone_gpu_profile_version);
}
inline void validate_standalone_gpu_profile(const nlohmann::json& report) {
    using Json = nlohmann::json;
    const auto require = [](bool value) {
        if (!value)
            throw std::runtime_error("Invalid standalone GPU profile observation");
    };
    const auto unsigned_value = [&](const Json& value) {
        require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0));
        return value.get<std::uint64_t>();
    };
    const auto text = [&](const Json& value, std::size_t maximum, bool empty = false) {
        require(value.is_string());
        const auto& result = value.get_ref<const std::string&>();
        require((empty || !result.empty()) && result.size() <= maximum &&
                result.find('\0') == std::string::npos);
    };
    require(report.at("format") == "forge.standalone-gpu-profile" &&
            unsigned_value(report.at("version")) == standalone_gpu_profile_version);
    text(report.at("session"), 128);
    text(report.at("build").at("id"), 128);
    text(report.at("build").at("source_commit"), 128);
    require(unsigned_value(report.at("captured_utc_us")) > 0);
    const auto& history = report.at("history");
    require(unsigned_value(history.at("capacity")) == 512);
    const auto sequence = unsigned_value(history.at("sequence"));
    (void)unsigned_value(history.at("epoch"));
    const auto& status = report.at("gpu");
    text(status.at("device").at("backend"), 32);
    text(status.at("device").at("adapter"), 512, true);
    require(status.at("device").at("software").is_boolean());
    for (const auto* field : {"compiled", "available", "enabled"})
        require(status.at(field).is_boolean());
    require((!status.at("available").get<bool>() || status.at("compiled").get<bool>()) &&
            (!status.at("enabled").get<bool>() || status.at("available").get<bool>()));
    text(status.at("reason"), 4096, true);
    const auto capacity = unsigned_value(status.at("query_capacity"));
    const auto created = unsigned_value(status.at("created"));
    require(capacity == 64 && created <= capacity &&
            unsigned_value(status.at("pending")) <= created);
    for (const auto* field : {"skipped", "stale", "latency_frames"})
        (void)unsigned_value(status.at(field));
    const auto submitted = unsigned_value(status.at("submitted_frame"));
    const auto& samples = status.at("samples");
    require(samples.is_array() && samples.size() <= 512);
    for (const auto& sample : samples) {
        require(sample.at("clock") == "gpu" && !sample.contains("tick") &&
                !sample.contains("start_seconds"));
        text(sample.at("category"), 128);
        text(sample.at("name"), 128);
        require(sample.at("seconds").is_number());
        const auto seconds = sample.at("seconds").get<double>();
        const auto frame = unsigned_value(sample.at("frame"));
        require(std::isfinite(seconds) && seconds >= 0 && seconds <= 3600 && frame &&
                frame <= submitted && unsigned_value(sample.at("sequence")) > 0 &&
                unsigned_value(sample.at("sequence")) <= sequence &&
                unsigned_value(sample.at("count")) == 1);
    }
}
inline std::string standalone_gpu_profile_record(const nlohmann::json& report) {
    validate_standalone_gpu_profile(report);
    const auto text = report.dump();
    if (text.size() > standalone_gpu_profile_bytes)
        throw std::runtime_error("Standalone GPU profile exceeds its byte limit");
    return std::string(standalone_gpu_profile_prefix) + text + '\n';
}
inline nlohmann::json standalone_gpu_profile_observation(nlohmann::json gpu,
                                                         const nlohmann::json& service_status,
                                                         const std::string& session,
                                                         const std::string& build,
                                                         const std::string& source_commit) {
    const auto captured = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    nlohmann::json result{{"format", "forge.standalone-gpu-profile"},
                          {"version", standalone_gpu_profile_version},
                          {"session", session},
                          {"build", {{"id", build}, {"source_commit", source_commit}}},
                          {"captured_utc_us", captured},
                          {"history",
                           {{"capacity", service_status.at("capacity")},
                            {"sequence", service_status.at("sequence")},
                            {"epoch", service_status.at("epoch")}}},
                          {"gpu", std::move(gpu)}};
    validate_standalone_gpu_profile(result);
    return result;
}
inline std::string standalone_gpu_profile_live_record(const nlohmann::json& report,
                                                      std::uint64_t ordinal) {
    validate_standalone_gpu_profile(report);
    if (!ordinal)
        throw std::runtime_error("Live GPU observation requires a positive ordinal");
    const auto text = nlohmann::json{
        {"format", "forge.standalone-gpu-live"},
        {"version", standalone_gpu_profile_live_version},
        {"ordinal", ordinal},
        {"report",
         report}}.dump();
    if (text.size() > standalone_gpu_profile_bytes)
        throw std::runtime_error("Standalone GPU profile exceeds its byte limit");
    return std::string(standalone_gpu_profile_live_prefix) + text + '\n';
}

// One run-scoped receiver. Ordinary game logs never become profile observations.
// A candidate is exposed only after the supervisor proves successful retirement.
class StandaloneGpuProfileReceiver {
    std::string line_, error_;
    nlohmann::json candidate_, accepted_, live_, expected_build_;
    std::uint64_t ordinal_ = 0;
    bool live_enabled_ = false;
    bool skip_line_ = false, suppress_line_ = false, finished_ = false;
    static nlohmann::json page_of(const nlohmann::json& observation, std::size_t offset,
                                  std::size_t limit) {
        const auto& samples = observation.at("gpu").at("samples");
        if (!limit || limit > 128 || offset > samples.size())
            throw std::out_of_range("Invalid standalone GPU observation page");
        const auto end = std::min(samples.size(), offset + limit);
        auto report = observation;
        report["gpu"]["samples"] = nlohmann::json::array();
        for (auto i = offset; i < end; ++i)
            report["gpu"]["samples"].push_back(samples[i]);
        return {{"sample_count", samples.size()},
                {"offset", offset},
                {"next_offset", end < samples.size() ? nlohmann::json(end) : nlohmann::json()},
                {"report", std::move(report)}};
    }
    void matching_identity(const nlohmann::json& report) const {
        if (!expected_build_.is_null() && expected_build_ != report.at("build"))
            throw std::runtime_error("GPU observation differs from the verified exported build");
        if (!live_.is_null() && (live_.at("session") != report.at("session") ||
                                 live_.at("build") != report.at("build") ||
                                 live_.at("gpu").at("device") != report.at("gpu").at("device")))
            throw std::runtime_error("Live GPU observation changed its host identity");
        if (!live_.is_null()) {
            const auto epoch = report.at("history").at("epoch").get<std::uint64_t>();
            const auto prior_epoch = live_.at("history").at("epoch").get<std::uint64_t>();
            if (epoch < prior_epoch ||
                (epoch == prior_epoch &&
                 report.at("history").at("sequence").get<std::uint64_t>() <
                     live_.at("history").at("sequence").get<std::uint64_t>()) ||
                report.at("gpu").at("submitted_frame").get<std::uint64_t>() <
                    live_.at("gpu").at("submitted_frame").get<std::uint64_t>())
                throw std::runtime_error("Live GPU observation moved its native history backwards");
        }
    }

  public:
    explicit StandaloneGpuProfileReceiver(bool live = false, nlohmann::json expected_build = {})
        : expected_build_(std::move(expected_build)), live_enabled_(live) {}
    const nlohmann::json& report() const { return accepted_; }
    const nlohmann::json& live_report() const { return live_; }
    bool live_enabled() const { return live_enabled_; }
    std::string live_snapshot() const {
        return live_.is_null()
                   ? ""
                   : live_.at("session").get<std::string>() + ":" + std::to_string(ordinal_);
    }
    const std::string& error() const { return error_; }
    bool finished() const { return finished_; }
    nlohmann::json page(std::size_t offset = 0, std::size_t limit = 64) const {
        if (accepted_.is_null())
            throw std::runtime_error("Wait for an accepted standalone GPU observation");
        return page_of(accepted_, offset, limit);
    }
    nlohmann::json live_page(const std::string& snapshot = {}, std::size_t offset = 0,
                             std::size_t limit = 64) const {
        if (live_.is_null())
            throw std::runtime_error("Wait for a provisional live GPU observation");
        if (offset && snapshot.empty())
            throw std::invalid_argument("Later live GPU pages require their snapshot token");
        if (!snapshot.empty() && snapshot != live_snapshot())
            throw std::out_of_range("Live GPU snapshot changed; read the first page again");
        auto result = page_of(live_, offset, limit);
        result["snapshot"] = live_snapshot();
        result["provisional"] = true;
        return result;
    }
    // Only live-mode wire records are consumed here; ordinary logs keep their
    // existing presentation owner. Default final-report mode returns raw bytes.
    std::string append(std::string_view bytes) {
        if (finished_)
            throw std::runtime_error("Standalone GPU profile receiver has finished");
        std::string visible = live_enabled_ ? "" : std::string(bytes);
        for (const char byte : bytes) {
            if (live_enabled_ && skip_line_ && !suppress_line_)
                visible.push_back(byte);
            if (byte == '\n') {
                const bool live =
                    live_enabled_ && line_.starts_with(standalone_gpu_profile_live_prefix);
                if (live_enabled_ && !skip_line_ && !live &&
                    !line_.starts_with(standalone_gpu_profile_prefix))
                    visible += line_ + '\n';
                if (!skip_line_ && (live || line_.starts_with(standalone_gpu_profile_prefix))) {
                    try {
                        if (!candidate_.is_null())
                            throw std::runtime_error(
                                "Duplicate standalone GPU profile observation");
                        const auto payload = std::string_view(line_).substr(
                            live ? standalone_gpu_profile_live_prefix.size()
                                 : standalone_gpu_profile_prefix.size());
                        auto value = asset_detail::parse_bounded_json(
                            std::as_bytes(std::span(payload.data(), payload.size())),
                            standalone_gpu_profile_bytes, 32768, 16);
                        if (live) {
                            const auto& ordinal = value.at("ordinal");
                            if (value.size() != 4 ||
                                value.at("format") != "forge.standalone-gpu-live" ||
                                !standalone_gpu_profile_version_matches(
                                    value.at("version"), standalone_gpu_profile_live_version) ||
                                !(ordinal.is_number_unsigned() ||
                                  (ordinal.is_number_integer() &&
                                   ordinal.get<std::int64_t>() > 0)) ||
                                ordinal_ == std::numeric_limits<std::uint64_t>::max() ||
                                ordinal.get<std::uint64_t>() != ordinal_ + 1)
                                throw std::runtime_error("Invalid live GPU observation sequence");
                            auto report = std::move(value.at("report"));
                            validate_standalone_gpu_profile(report);
                            matching_identity(report);
                            if (error_.empty()) {
                                live_ = std::move(report);
                                ++ordinal_;
                            }
                        } else {
                            validate_standalone_gpu_profile(value);
                            matching_identity(value);
                            candidate_ = std::move(value);
                        }
                    } catch (const std::exception& e) {
                        error_ = e.what();
                        live_ = nullptr;
                    }
                }
                line_.clear();
                skip_line_ = false;
                suppress_line_ = false;
            } else if (!skip_line_) {
                line_.push_back(byte);
                const bool possible_final = standalone_gpu_profile_prefix.starts_with(line_) ||
                                            line_.starts_with(standalone_gpu_profile_prefix);
                const bool possible_live =
                    live_enabled_ && (standalone_gpu_profile_live_prefix.starts_with(line_) ||
                                      line_.starts_with(standalone_gpu_profile_live_prefix));
                if (!possible_final && !possible_live) {
                    if (live_enabled_)
                        visible += line_;
                    line_.clear();
                    skip_line_ = true;
                } else if (line_.size() >
                           standalone_gpu_profile_prefix.size() + standalone_gpu_profile_bytes) {
                    line_.clear();
                    skip_line_ = true;
                    suppress_line_ = true;
                    error_ = "Standalone GPU profile exceeds its byte limit";
                    live_ = nullptr;
                }
            }
        }
        return visible;
    }
    std::string finish(bool successful_retirement) {
        if (finished_)
            throw std::runtime_error("Standalone GPU profile receiver has finished");
        finished_ = true;
        const auto visible = live_enabled_ && !skip_line_ &&
                                     !line_.starts_with(standalone_gpu_profile_live_prefix) &&
                                     !line_.starts_with(standalone_gpu_profile_prefix)
                                 ? line_
                                 : std::string{};
        if (!successful_retirement)
            error_ = "Game did not exit successfully; GPU observations were not accepted";
        else if (!skip_line_ &&
                 (line_.starts_with(standalone_gpu_profile_prefix) ||
                  (live_enabled_ && line_.starts_with(standalone_gpu_profile_live_prefix))))
            error_ = "Standalone GPU profile record is incomplete";
        else if (candidate_.is_null() && error_.empty())
            error_ = "Game returned no standalone GPU profile observation";
        if (error_.empty())
            accepted_ = std::move(candidate_);
        candidate_ = nullptr;
        live_ = nullptr;
        line_.clear();
        return visible;
    }
};
} // namespace forge
