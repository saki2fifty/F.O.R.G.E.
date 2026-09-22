#pragma once
#include "import_job_cleanup.hpp"
#include "worker_stage_lease.hpp"

void check_import_job_cleanup(const ProjectLease& lease) {
    using namespace forge::asset_detail;
    const auto root = lease.root();
    struct Job {
        std::filesystem::path path;
        std::unique_ptr<WorkerStageLease> owner;
    };
    auto make = [&](const char* kind) {
        const auto id = AssetId::generate();
        const auto path = root / ".forge/jobs" / id.str();
        std::filesystem::create_directories(path);
        const auto marker = nlohmann::json{
            {"format", "forge.import-job"},
            {"version", 1},
            {"job", id.str()},
            {"kind", kind}}.dump();
        return Job{path, std::make_unique<WorkerStageLease>(path / "owner.lock", marker)};
    };
    auto input = [&](const Job& job) {
        write(
            job.path / "request.json",
            R"({"format":"forge.import-worker","version":1,"payload":{},"inputs":[{"name":"part.bin"}]})");
        write(job.path / "input/part.bin", "partial source snapshot");
    };
    auto live = make("import");
    input(live);
    write(live.path / "output/partial.bin", "unfinished output");
    auto result = cleanup_import_jobs(lease);
    check(result.size() == 1 && result[0]["removed"] == false &&
              std::filesystem::exists(live.path / "input/part.bin"),
          "Cleanup removed an actively owned job");
    live.owner.reset();
    result = cleanup_import_jobs(lease);
    check(result.size() == 1 && result[0]["removed"] == true &&
              !std::filesystem::exists(live.path) && cleanup_import_jobs(lease).empty(),
          "Abandoned import stage was not safely removed/idempotent");
    auto partial = make("import");
    std::filesystem::create_directory(partial.path / "input");
    partial.owner.reset();
    auto animation = make("model-animation");
    write(animation.path / "config.json", "interrupted write");
    write(animation.path / "clip-63.ozz", "partial output");
    animation.owner.reset();
    result = cleanup_import_jobs(lease);
    check(result.size() == 2 && result[0]["removed"] == true && result[1]["removed"] == true,
          "Recognized partial preparation/conversion was retained");

    auto unknown = make("import");
    input(unknown);
    write(unknown.path / "user-notes.txt", "preserve");
    unknown.owner.reset();
    auto nested = make("import");
    input(nested);
    write(nested.path / "output/unexpected/note.txt", "preserve");
    nested.owner.reset();
    auto malformed = make("import");
    write(malformed.path / "request.json", "{truncated");
    malformed.owner.reset();
    auto future = make("future-version");
    future.owner.reset();
    auto alias = make("import");
    input(alias);
    std::filesystem::create_hard_link(alias.path / "input/part.bin", root / "shared-input.bin");
    alias.owner.reset();
    const auto old = root / ".forge/jobs" / AssetId::generate().str();
    write(old / "request.json", "old unmarked stage");
    auto wrong_marker = make("import");
    wrong_marker.owner.reset();
    write(wrong_marker.path / "owner.lock", "malformed marker");
    auto redirected = make("import");
#ifndef _WIN32
    std::filesystem::create_directory_symlink(root / "Assets", redirected.path / "output");
#else
    // A noncanonical converter filename exercises the same whole-job preflight
    // on hosts where creating symbolic links requires extra OS privileges.
    write(redirected.path / "unrecognized.txt", "preserve");
#endif
    redirected.owner.reset();
    result = cleanup_import_jobs(lease);
    check(result.size() == 8 &&
              std::all_of(result.begin(), result.end(),
                          [](const auto& row) { return row.at("removed") == false; }) &&
              std::filesystem::exists(unknown.path / "input/part.bin") &&
              std::filesystem::exists(nested.path / "input/part.bin") &&
              read_bytes(root / "shared-input.bin", 128).size() == 23,
          "Unfamiliar/aliased worker storage was removed or preflight partially deleted a job");
    const auto maintenance = maintain_asset_cache(lease, CacheMaintenance::Cleanup);
    check(maintenance.at("ok") == false && maintenance.at("worker_jobs").size() == 8,
          "Writer-owned cleanup did not expose retained worker diagnostics");
    std::stop_source cancelled;
    cancelled.request_stop();
    rejects([&] { cleanup_import_jobs(lease, cancelled.get_token()); });
    check(std::filesystem::exists(unknown.path / "user-notes.txt"),
          "Cancelled cleanup changed staging");
    for (const auto& path : {unknown.path, nested.path, malformed.path, future.path, alias.path,
                             wrong_marker.path, redirected.path, old})
        std::filesystem::remove_all(path); // Only this test's explicitly created fixture files.
    std::filesystem::remove(root / "shared-input.bin");
}
