#include "standalone_gpu_profile.hpp"
#include <iostream>
#include <limits>
using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void refused(F&& operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid GPU observation was accepted");
}
Json observation() {
    // Structural fixture only: these values are not measured GPU timings.
    return {
        {"format", "forge.standalone-gpu-profile"},
        {"version", 1},
        {"session", "test-host-session"},
        {"build", {{"id", "test"}, {"source_commit", "test-source"}}},
        {"captured_utc_us", 1u},
        {"history", {{"capacity", 512u}, {"sequence", 1u}, {"epoch", 0u}}},
        {"gpu",
         {{"compiled", true},
          {"device", {{"backend", "test"}, {"adapter", "Structural fixture"}, {"software", true}}},
          {"available", true},
          {"enabled", true},
          {"reason", ""},
          {"query_capacity", 64u},
          {"created", 2u},
          {"pending", 1u},
          {"skipped", 0u},
          {"stale", 0u},
          {"latency_frames", 1u},
          {"submitted_frame", 3u},
          {"samples", Json::array({{{"clock", "gpu"},
                                    {"category", "Game"},
                                    {"name", "Camera geometry"},
                                    {"seconds", .001},
                                    {"count", 1u},
                                    {"sequence", 1u},
                                    {"frame", 2u}}})}}}};
}
void chunks() {
    const auto value = observation();
    const auto record = standalone_gpu_profile_record(value);
    for (std::size_t split = 0; split <= record.size(); ++split) {
        StandaloneGpuProfileReceiver receiver;
        receiver.append("Ordinary game output\nFORGE standalone startup verified\n");
        receiver.append(std::string_view(record).substr(0, split));
        check(receiver.report().is_null(), "Report escaped before native retirement");
        refused([&] { receiver.page(); });
        receiver.append(std::string_view(record).substr(split));
        receiver.append("Shutdown complete\nF");
        receiver.finish(true);
        check(receiver.error().empty() && receiver.report() == value,
              "Chunked native GPU record changed or lost its observation");
        refused([&] { receiver.finish(true); });
        refused([&] { receiver.append(record); });
    }
    StandaloneGpuProfileReceiver bytes;
    for (const char c : record)
        bytes.append(std::string_view(&c, 1));
    bytes.finish(true);
    check(bytes.report() == value, "One-byte chunks lost the GPU record");
    StandaloneGpuProfileReceiver windows;
    windows.append(std::string_view(record).substr(0, record.size() - 1));
    windows.append("\r\n");
    windows.finish(true);
    check(windows.report() == value, "Windows native line ending changed the GPU record");
}
void boundaries() {
    const auto original = observation();
    const auto captured = standalone_gpu_profile_observation(
        original.at("gpu"), original.at("history"), "native-session", "test-build", "test-source");
    check(captured.at("gpu") == original.at("gpu") && captured.at("captured_utc_us") > 0 &&
              captured.at("session") == "native-session",
          "Producer changed native observations or lost timestamp/session provenance");
    check(standalone_gpu_profile_supported({{"runtime_gpu_profiling", 1}}) &&
              !standalone_gpu_profile_supported(Json::object()) &&
              !standalone_gpu_profile_supported({{"runtime_gpu_profiling", true}}) &&
              !standalone_gpu_profile_supported({{"runtime_gpu_profiling", 1.0}}) &&
              !standalone_gpu_profile_supported({{"runtime_gpu_profiling", 2}}),
          "Runtime-kit profile admission accepted an old/malformed capability");
    auto value = original;
    value["history"]["sequence"] = 512;
    value["gpu"]["samples"] = Json::array();
    for (unsigned i = 0; i < 512; ++i) {
        auto row = original["gpu"]["samples"][0];
        row["sequence"] = i + 1;
        value["gpu"]["samples"].push_back(std::move(row));
    }
    StandaloneGpuProfileReceiver maximum;
    maximum.append(standalone_gpu_profile_record(value));
    maximum.finish(true);
    check(maximum.report() == value, "Native service's full bounded history was rejected");
    Json pages = Json::array();
    for (std::size_t offset = 0; offset < 512;) {
        const auto page = maximum.page(offset, 97);
        check(page.at("sample_count") == 512 && page.at("offset") == offset &&
                  page.at("report").at("session") == value.at("session"),
              "GPU report pagination lost its immutable host scope");
        for (const auto& sample : page.at("report").at("gpu").at("samples"))
            pages.push_back(sample);
        if (page.at("next_offset").is_null())
            break;
        offset = page.at("next_offset");
    }
    check(pages == value.at("gpu").at("samples") && maximum.report() == value &&
              maximum.page(512).at("report").at("gpu").at("samples").empty(),
          "GPU pages lost/duplicated samples or changed the accepted report");
    refused([&] { maximum.page(513); });
    refused([&] { maximum.page(0, 0); });
    refused([&] { maximum.page(0, 129); });
    value["gpu"]["samples"].push_back(original["gpu"]["samples"][0]);
    refused([&] { standalone_gpu_profile_record(value); });
    const auto reject_field = [&](const char* pointer, Json invalid) {
        auto bad = original;
        bad[Json::json_pointer(pointer)] = std::move(invalid);
        refused([&] { standalone_gpu_profile_record(bad); });
    };
    reject_field("/version", 1.0);
    reject_field("/captured_utc_us", -1);
    reject_field("/gpu/query_capacity", 65);
    reject_field("/gpu/created", 65);
    reject_field("/gpu/pending", 3);
    reject_field("/gpu/skipped", true);
    reject_field("/gpu/compiled", false);
    reject_field("/gpu/available", false);
    reject_field("/gpu/samples/0/clock", "cpu");
    reject_field("/gpu/samples/0/frame", 4);
    reject_field("/gpu/samples/0/seconds", -1.);
    reject_field("/gpu/samples/0/seconds", std::numeric_limits<double>::infinity());
    reject_field("/gpu/samples/0/name", std::string(129, 'x'));
    value = original;
    value["gpu"]["samples"][0]["tick"] = 2;
    refused([&] { standalone_gpu_profile_record(value); });
    value = original;
    value["gpu"]["available"] = value["gpu"]["enabled"] = false;
    value["gpu"]["created"] = value["gpu"]["pending"] = 0;
    value["gpu"]["samples"] = Json::array();
    value["gpu"]["reason"] = "This device does not support GPU duration queries.";
    StandaloneGpuProfileReceiver unsupported;
    unsupported.append(standalone_gpu_profile_record(value));
    unsupported.finish(true);
    check(unsupported.report() == value, "Unsupported GPU was replaced by a fake CPU fallback");
}
void failures() {
    const auto record = standalone_gpu_profile_record(observation());
    for (const auto& input :
         {record + record, record.substr(0, record.size() - 1),
          std::string(standalone_gpu_profile_prefix) + "{}\n",
          std::string(standalone_gpu_profile_prefix) + "{\"format\":0,\"format\":1}\n",
          std::string(standalone_gpu_profile_prefix) +
              std::string(standalone_gpu_profile_bytes + 1, 'x') + '\n'}) {
        StandaloneGpuProfileReceiver receiver;
        receiver.append(input);
        receiver.finish(true);
        check(receiver.report().is_null() && !receiver.error().empty(),
              "Bad or duplicate GPU records became successful observations");
    }
    StandaloneGpuProfileReceiver terminated;
    terminated.append(record);
    terminated.finish(false);
    check(terminated.report().is_null() && !terminated.error().empty(),
          "Forced/failed native exit established GPU acceptance");
    StandaloneGpuProfileReceiver noisy;
    noisy.append(std::string(2 * standalone_gpu_profile_bytes, 'x'));
    noisy.append("\n");
    noisy.append(record);
    noisy.finish(true);
    check(noisy.report() == observation(), "Unrelated oversized game log stole the GPU record");
}
} // namespace
int main() {
    try {
        chunks();
        boundaries();
        failures();
        std::cout << "GPU report framing, bounds, typed observations, unsupported devices and "
                     "successful native retirement admission passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
