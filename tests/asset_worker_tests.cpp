#include "asset_worker.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge::asset_detail;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
#include "worker_stage_tests.hpp"
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Need worker and private test directory");
        const auto executable = std::filesystem::absolute(argv[1]);
        const auto parent = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(parent);
        const auto root =
            parent /
            ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root))
            throw std::runtime_error("Cannot create private worker fixture directory");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        WorkerLimits limits;
        limits.memory_bytes = 128ull * 1024 * 1024;
        limits.file_bytes = 4096;
        limits.total_bytes = 8192;
        limits.seconds = 1;
        limits.cpu_seconds = 2;
        limits.files = 8;
        limits.cancellation_grace_ms = 300;
        check_worker_stage_inheritance(executable, root, limits);
        unsigned sequence = 0;
        auto run = [&](const char* mode, bool reject, std::stop_token cancel = {}) {
            const auto staging = root / std::to_string(++sequence);
            std::filesystem::create_directories(staging / "output");
            std::ofstream(staging / "request.txt") << mode;
            bool rejected = false;
            try {
                run_worker(WorkerKind::Import, executable, staging, cancel, limits);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected == reject, (std::string("Worker policy mismatch: ") + mode).c_str());
            return staging;
        };
        run("limits", false);
        for (auto mode : {"fail", "file", "total", "count", "nested"})
            run(mode, true);
        std::stop_source cooperate;
        std::jthread request([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cooperate.request_stop();
        });
        auto begin = std::chrono::steady_clock::now();
        auto staging = run("cooperate", true, cooperate.get_token());
        require(std::filesystem::is_regular_file(staging / "output/cancelled.txt"),
                "Cooperative cancellation was not requested");
        require(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2),
                "Cooperative cancellation was slow");
        std::stop_source stubborn;
        std::jthread request2([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stubborn.request_stop();
        });
        begin = std::chrono::steady_clock::now();
        run("delay", true, stubborn.get_token());
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        require(elapsed >= std::chrono::milliseconds(300) && elapsed < std::chrono::seconds(2),
                "Cancellation grace/termination was not bounded");
        begin = std::chrono::steady_clock::now();
        run("delay", true);
        require(std::chrono::steady_clock::now() - begin >= std::chrono::milliseconds(900),
                "Timeout ended too early");
        auto pristine = root / "pristine";
        std::filesystem::create_directories(pristine / "output");
        std::ofstream(pristine / "request.txt") << "limits";
        std::stop_source stopped;
        stopped.request_stop();
        bool rejected = false;
        try {
            run_worker(WorkerKind::Import, executable, pristine, stopped.get_token(), limits);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && !std::filesystem::exists(pristine / "output/valid.bin"),
                "Cancelled work launched a child");
        std::filesystem::create_directory(pristine / "cancel.request");
        rejected = false;
        try {
            run_worker(WorkerKind::Import, executable, pristine, {}, limits);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Stale cancellation staging was reused");
        std::cout << "Asset worker limits, output shape, cancellation/grace and timeout passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
