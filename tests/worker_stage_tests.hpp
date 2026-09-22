#pragma once
#include "worker_stage_lease.hpp"
#include <future>

void check_worker_stage_inheritance(const std::filesystem::path& executable,
                                    const std::filesystem::path& root, WorkerLimits limits) {
    const auto stage = root / "inherited-stage";
    std::filesystem::create_directories(stage / "output");
    const auto marker = stage / "owner.lock";
    auto owner = std::make_unique<WorkerStageLease>(marker, "fixture ownership");
    require(!WorkerStageLease::try_open(marker), "Live worker stage was not exclusive");
    std::ofstream(stage / "request.txt") << "lease";
    std::ofstream(stage / "lease-handle.txt") << owner->inheritance_handle();
    limits.seconds = 5;
    const auto* borrowed = owner.get();
    auto worker = std::async(std::launch::async, [&, borrowed] {
        run_worker(WorkerKind::Import, executable, stage, {}, limits, borrowed);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!std::filesystem::exists(stage / "output/started")) {
        require(std::chrono::steady_clock::now() < deadline &&
                    worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
                "Worker did not inherit its staging handle");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // The worker has completed startup, the supervisor never reads borrowed
    // again, and only the child's inherited object can now keep this lock held.
    owner.reset();
    require(!WorkerStageLease::try_open(marker),
            "Parent close released storage still owned by a running child");
    std::ofstream(stage / "continue.request") << "finish";
    worker.get();
    auto reclaimed = WorkerStageLease::try_open(marker);
    require(reclaimed && reclaimed->marker() == "fixture ownership",
            "Joined worker did not release intact storage ownership");
}
