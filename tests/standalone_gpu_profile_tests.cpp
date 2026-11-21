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
void live_observations() {
    const auto first = observation();
    auto second = first;
    second["captured_utc_us"] = 2;
    second["history"]["sequence"] = 2;
    second["gpu"]["submitted_frame"] = 4;
    auto extra = first.at("gpu").at("samples")[0];
    extra["sequence"] = 2;
    extra["frame"] = 3;
    second["gpu"]["samples"].push_back(extra);
    const auto first_record = standalone_gpu_profile_live_record(first, 1);
    const auto second_record = standalone_gpu_profile_live_record(second, 2);
    for (std::size_t split = 0; split <= first_record.size(); ++split) {
        StandaloneGpuProfileReceiver receiver(true);
        receiver.append(std::string_view(first_record).substr(0, split));
        receiver.append(std::string_view(first_record).substr(split));
        check(receiver.live_report() == first && receiver.report().is_null() &&
                  !receiver.finished(),
              "Live observation was lost or became a final report");
        const auto page = receiver.live_page();
        check(page.at("provisional") == true && page.at("report") == first &&
                  page.at("sample_count") == 1,
              "Live first page lost its provisional status");
        const auto token = page.at("snapshot").get<std::string>();
        refused([&] { receiver.page(); });
        refused([&] { receiver.live_page({}, 1); });
        receiver.append(second_record);
        check(receiver.live_report() == second && receiver.report().is_null() &&
                  receiver.live_snapshot() != token,
              "New live data did not replace the bounded snapshot");
        refused([&] { receiver.live_page(token); });
        const auto current = receiver.live_snapshot();
        check(receiver.live_page(current, 1, 1).at("report").at("gpu").at("samples")[0] == extra,
              "Guarded live paging lost sample correspondence");
        receiver.append(standalone_gpu_profile_record(second));
        check(receiver.report().is_null(), "An early final record bypassed retirement");
        receiver.finish(true);
        check(receiver.report() == second && receiver.live_report().is_null() &&
                  receiver.live_snapshot().empty(),
              "Successful retirement did not replace provisional data");
        refused([&] { receiver.live_page(current); });
    }
    StandaloneGpuProfileReceiver verified(true, first.at("build"));
    verified.append(first_record);
    check(verified.live_report() == first,
          "Verified exported build rejected its matching observation");
    auto foreign_build = first.at("build");
    foreign_build["source_commit"] = "foreign-build";
    for (const bool live : {false, true}) {
        StandaloneGpuProfileReceiver foreign(live, foreign_build);
        foreign.append(live ? first_record : standalone_gpu_profile_record(first));
        foreign.finish(true);
        check(foreign.report().is_null() && foreign.live_report().is_null() &&
                  !foreign.error().empty(),
              "A foreign first report bypassed the verified exported build identity");
    }
    StandaloneGpuProfileReceiver old_receiver;
    old_receiver.append(first_record + standalone_gpu_profile_record(first));
    old_receiver.finish(true);
    check(old_receiver.report() == first,
          "Default completed-report mode admitted a live protocol change");
    StandaloneGpuProfileReceiver cancelled(true);
    cancelled.append(first_record);
    cancelled.finish(false);
    check(cancelled.live_report().is_null() && cancelled.report().is_null() &&
              !cancelled.error().empty(),
          "Cancelled child retained an accepted or provisional report");
    const Json kit{{"runtime_gpu_profiling", 1}, {"runtime_gpu_profiling_live", 1}};
    check(standalone_gpu_profile_supported(kit, true) &&
              !standalone_gpu_profile_supported({{"runtime_gpu_profiling_live", 1}}, true) &&
              !standalone_gpu_profile_supported({{"runtime_gpu_profiling", 1}}, true),
          "Live launch did not require both exact kit capabilities");
    for (const auto& invalid :
         {Json(true), Json(1.0), Json(-4294967295LL), Json(std::uint64_t(4294967297ULL))}) {
        auto value = kit;
        value["runtime_gpu_profiling_live"] = invalid;
        check(!standalone_gpu_profile_supported(value, true),
              "Malformed live kit capability was accepted");
    }
    auto envelope = Json::parse(first_record.substr(standalone_gpu_profile_live_prefix.size()));
    for (const auto& pointer : {"/ordinal", "/version"}) {
        auto bad = envelope;
        bad[Json::json_pointer(pointer)] = 1.0;
        StandaloneGpuProfileReceiver receiver(true);
        receiver.append(std::string(standalone_gpu_profile_live_prefix) + bad.dump() + "\n");
        receiver.append(standalone_gpu_profile_record(first));
        receiver.finish(true);
        check(!receiver.error().empty() && receiver.report().is_null(),
              "Malformed live integer became a final report");
    }
    for (const auto& pointer :
         {"/session", "/build/id", "/build/source_commit", "/gpu/device/adapter"}) {
        auto changed = second;
        changed[Json::json_pointer(pointer)] = "different";
        StandaloneGpuProfileReceiver receiver(true);
        receiver.append(first_record + standalone_gpu_profile_live_record(changed, 2));
        check(!receiver.error().empty() && receiver.live_report().is_null(),
              "Foreign live identity remained visible");
        receiver.finish(true);
        check(receiver.report().is_null(), "Foreign live identity became an accepted final report");
    }
    for (const auto& bytes :
         {first_record + first_record, first_record + standalone_gpu_profile_live_record(second, 3),
          first_record.substr(0, first_record.size() - 1),
          first_record + standalone_gpu_profile_record(first) + second_record,
          std::string(standalone_gpu_profile_live_prefix) +
              std::string(standalone_gpu_profile_bytes + 1, 'x') + "\n"}) {
        StandaloneGpuProfileReceiver receiver(true);
        receiver.append(bytes);
        receiver.finish(true);
        check(receiver.report().is_null() && receiver.live_report().is_null() &&
                  !receiver.error().empty(),
              "Bad live framing/sequence became accepted or stayed visible");
    }
    auto reset = second;
    reset["history"]["epoch"] = 1;
    reset["history"]["sequence"] = 0;
    reset["gpu"]["samples"] = Json::array();
    StandaloneGpuProfileReceiver reset_receiver(true);
    reset_receiver.append(first_record + standalone_gpu_profile_live_record(reset, 2));
    check(reset_receiver.live_report() == reset, "A legitimate native history reset was rejected");
    reset_receiver.append(standalone_gpu_profile_live_record(second, 3));
    check(!reset_receiver.error().empty() && reset_receiver.live_report().is_null(),
          "A backwards native history epoch remained visible");
    const auto wire = std::string("before\r\nFORGE\n") + first_record + "middle\n" +
                      standalone_gpu_profile_record(first) + "F";
    for (const auto chunk : {1u, 7u, 1024u}) {
        StandaloneGpuProfileReceiver logs(true);
        std::string visible;
        for (std::size_t offset = 0; offset < wire.size(); offset += chunk)
            visible += logs.append(std::string_view(wire).substr(offset, chunk));
        visible += logs.finish(true);
        check(visible == "before\r\nFORGE\nmiddle\nF" && logs.report() == first,
              "Live wire filtering flooded or changed ordinary game logs");
    }
    StandaloneGpuProfileReceiver raw;
    check(raw.append(wire) == wire, "Default report mode changed existing raw game logs");
    refused([&] { standalone_gpu_profile_live_record(first, 0); });
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
        live_observations();
        std::cout << "GPU report framing, bounds, typed observations, unsupported devices and "
                     "successful native retirement admission passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
