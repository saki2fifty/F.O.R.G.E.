#pragma once
#include "bounded_json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace forge {
// A bounded observation on the graphical host's existing supervised stdout.
// The presentation and profiling services retain all query/sample ownership.
inline constexpr std::string_view standalone_gpu_profile_prefix = "FORGE GPU PROFILE ";
inline constexpr std::size_t standalone_gpu_profile_bytes = 1024 * 1024;
inline constexpr unsigned standalone_gpu_profile_version = 1;
inline bool standalone_gpu_profile_supported(const nlohmann::json& engine) {
    if (!engine.is_object())
        return false;
    const auto support = engine.find("runtime_gpu_profiling");
    return support != engine.end() && support->is_number_integer() &&
           *support == standalone_gpu_profile_version;
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

// One run-scoped receiver. Ordinary game logs never become profile observations.
// A candidate is exposed only after the supervisor proves successful retirement.
class StandaloneGpuProfileReceiver {
    std::string line_, error_;
    nlohmann::json candidate_, accepted_;
    bool skip_line_ = false, finished_ = false;

  public:
    const nlohmann::json& report() const { return accepted_; }
    const std::string& error() const { return error_; }
    bool finished() const { return finished_; }
    nlohmann::json page(std::size_t offset = 0, std::size_t limit = 64) const {
        if (accepted_.is_null())
            throw std::runtime_error("Wait for an accepted standalone GPU observation");
        const auto& samples = accepted_.at("gpu").at("samples");
        if (!limit || limit > 128 || offset > samples.size())
            throw std::out_of_range("Invalid standalone GPU observation page");
        const auto end = std::min(samples.size(), offset + limit);
        auto report = accepted_;
        report["gpu"]["samples"] = nlohmann::json::array();
        for (auto i = offset; i < end; ++i)
            report["gpu"]["samples"].push_back(samples[i]);
        return {{"sample_count", samples.size()},
                {"offset", offset},
                {"next_offset", end < samples.size() ? nlohmann::json(end) : nlohmann::json()},
                {"report", std::move(report)}};
    }
    void append(std::string_view bytes) {
        if (finished_)
            throw std::runtime_error("Standalone GPU profile receiver has finished");
        for (const char byte : bytes) {
            if (byte == '\n') {
                if (!skip_line_ && line_.starts_with(standalone_gpu_profile_prefix)) {
                    try {
                        if (!candidate_.is_null())
                            throw std::runtime_error(
                                "Duplicate standalone GPU profile observation");
                        const auto payload =
                            std::string_view(line_).substr(standalone_gpu_profile_prefix.size());
                        auto value = asset_detail::parse_bounded_json(
                            std::as_bytes(std::span(payload.data(), payload.size())),
                            standalone_gpu_profile_bytes, 32768, 16);
                        validate_standalone_gpu_profile(value);
                        candidate_ = std::move(value);
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                line_.clear();
                skip_line_ = false;
            } else if (!skip_line_) {
                line_.push_back(byte);
                if (line_.size() <= standalone_gpu_profile_prefix.size() &&
                    byte != standalone_gpu_profile_prefix[line_.size() - 1]) {
                    line_.clear();
                    skip_line_ = true;
                } else if (line_.size() >
                           standalone_gpu_profile_prefix.size() + standalone_gpu_profile_bytes) {
                    line_.clear();
                    skip_line_ = true;
                    error_ = "Standalone GPU profile exceeds its byte limit";
                }
            }
        }
    }
    void finish(bool successful_retirement) {
        if (finished_)
            throw std::runtime_error("Standalone GPU profile receiver has finished");
        finished_ = true;
        if (!successful_retirement)
            error_ = "Game did not exit successfully; GPU observations were not accepted";
        else if (!skip_line_ && line_.starts_with(standalone_gpu_profile_prefix))
            error_ = "Standalone GPU profile record is incomplete";
        else if (candidate_.is_null() && error_.empty())
            error_ = "Game returned no standalone GPU profile observation";
        if (error_.empty())
            accepted_ = std::move(candidate_);
        candidate_ = nullptr;
        line_.clear();
    }
};
} // namespace forge
