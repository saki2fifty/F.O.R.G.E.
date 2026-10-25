#pragma once
#include "worker_stage_lease.hpp"
#include <SDL3/SDL.h>
#include <future>

void check_worker_stage_isolation(const std::filesystem::path& executable,
                                  const std::filesystem::path& root) {
    const auto stage = root / "unrelated-stage";
    std::filesystem::create_directories(stage);
    const auto marker = stage / "owner.lock";
    auto owner = std::make_unique<WorkerStageLease>(marker, "unrelated fixture ownership");
    const auto executable_u8 = executable.u8string(), stage_u8 = stage.u8string();
    const std::string program(executable_u8.begin(), executable_u8.end());
    const std::string directory(stage_u8.begin(), stage_u8.end());
    const char* args[]{program.c_str(), "--unrelated-stage", directory.c_str(), nullptr};
    struct Child {
        SDL_Process* process;
        ~Child() {
            if (process) {
                SDL_KillProcess(process, true);
                SDL_WaitProcess(process, true, nullptr);
                SDL_DestroyProcess(process);
            }
        }
    } child{SDL_CreateProcess(args, false)};
    require(child.process, "Cannot start unrelated SDL child");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!std::filesystem::exists(stage / "started")) {
        require(std::chrono::steady_clock::now() < deadline &&
                    !SDL_WaitProcess(child.process, false, nullptr),
                "Unrelated SDL child did not start");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    owner.reset();
    auto reclaimed = WorkerStageLease::try_open(marker);
    require(reclaimed && reclaimed->marker() == "unrelated fixture ownership",
            "Unrelated SDL child retained another worker's staging ownership");
    std::ofstream(stage / "continue.request") << "finish";
    int code = -1;
    require(SDL_WaitProcess(child.process, true, &code) && code == 0, "Unrelated SDL child failed");
}

void check_worker_stage_inheritance(const std::filesystem::path& executable,
                                    const std::filesystem::path& root, WorkerLimits limits) {
    const auto stage = root / "inherited-stage";
    std::filesystem::create_directories(stage / "output");
    const auto marker = stage / "owner.lock";
    auto owner = std::make_unique<WorkerStageLease>(marker, "fixture ownership");
    require(!WorkerStageLease::try_open(marker), "Live worker stage was not exclusive");
    std::ofstream(stage / "request.txt") << "lease";
    limits.seconds = 5;
    const auto* borrowed = owner.get();
    auto worker = std::async(std::launch::async, [&, borrowed] {
        run_worker(WorkerKind::Import, executable, stage, {}, limits, borrowed);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!std::filesystem::exists(stage / "output/started")) {
        require(std::chrono::steady_clock::now() < deadline &&
                    worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
                "Worker did not start with its staging ownership");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // The worker has completed startup, the supervisor never reads borrowed
    // again, and only the child's copy can now keep this lock held. Its native
    // handle value need not equal the parent's handle value.
    owner.reset();
    require(!WorkerStageLease::try_open(marker),
            "Parent close released storage still owned by a running child");
    std::ofstream(stage / "continue.request") << "finish";
    worker.get();
    auto reclaimed = WorkerStageLease::try_open(marker);
    require(reclaimed && reclaimed->marker() == "fixture ownership",
            "Joined worker did not release intact storage ownership");
}
