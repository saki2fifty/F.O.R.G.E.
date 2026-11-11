#include "import_process.hpp"
#include "worker_stage_lease.hpp"
#include "worker_staging.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, std::string_view value) {
    std::ofstream out(path, std::ios::binary);
    check(bool(out << value) && bool(out.flush()), "Cannot write import fixture");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void wait_file(const std::filesystem::path& path) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(path)) {
        check(std::chrono::steady_clock::now() < end, "Import fixture handshake timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int worker() {
    const auto stage = std::filesystem::current_path();
    const auto request = read_import_process_request(stage, {});
    const auto mode = request.payload.at("mode").get<std::string>();
    const auto control = std::filesystem::u8path(request.payload.at("control").get<std::string>());
    write(control / "stage", path_utf8(stage));
    if (mode == "fail") {
        write_import_process_status(stage, "fixture pending entry");
        write_import_process_error(stage, "fixture.failed", "expected worker diagnostic");
        return 7;
    }
    if (mode == "partial-status") {
        write(stage / "status.json", "{unfinished");
        return 7;
    }
    if (mode == "unknown")
        write(stage / "user-notes.txt", "retain for inspection");
#ifdef __linux__
    if (mode == "escaped") {
        const auto child = fork();
        check(child >= 0, "Cannot fork import fixture descendant");
        if (child == 0) {
            try {
                check(setsid() >= 0, "Cannot detach fixture session");
                write(control / "ready", "ready");
                wait_file(control / "release");
                _exit(0);
            } catch (...) {
                _exit(21);
            }
        }
        try {
            write(control / "pid", std::to_string(child));
            wait_file(control / "ready");
        } catch (...) {
            kill(child, SIGKILL);
            while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
            }
            throw;
        }
    }
#endif
    write_import_process_result(stage, {{"result.bin", {std::byte{42}}}}, {});
    return 0;
}

#ifdef __linux__
// This isolated test process adopts only its fixture descendants; the editor
// never changes global child-reaping behavior. Reap even when an assertion fails.
struct Descendant {
    std::filesystem::path control;
    int previous = 0;
    pid_t child = -1;
    explicit Descendant(std::filesystem::path path) : control(std::move(path)) {
        check(prctl(PR_GET_CHILD_SUBREAPER, &previous, 0L, 0L, 0L) == 0 &&
                  prctl(PR_SET_CHILD_SUBREAPER, 1L, 0L, 0L, 0L) == 0,
              "Cannot isolate fixture descendant ownership");
    }
    pid_t id() {
        if (child < 0 && std::filesystem::exists(control / "pid"))
            child = pid_t(std::stol(read(control / "pid")));
        return child;
    }
    void finish() {
        check(id() > 0, "Missing fixture descendant identity");
        write(control / "release", "release");
        int status = 0;
        pid_t result;
        do {
            result = waitpid(child, &status, 0);
        } while (result < 0 && errno == EINTR);
        child = 0;
        check(result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "Fixture descendant did not retire cleanly");
    }
    ~Descendant() {
        try {
            if (id() > 0) {
                kill(child, SIGKILL);
                while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
                }
            }
        } catch (...) {
        }
        prctl(PR_SET_CHILD_SUBREAPER, long(previous), 0L, 0L, 0L);
    }
};
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--build-asset")
            return worker();
        check(argc == 3, "Need scratch directory and fixture mode");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto control = root / "control";
        std::filesystem::create_directories(control);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        ProjectLease lease(root);
        const auto executable = std::filesystem::absolute(argv[0]);
        write(root / "source.txt", "saved source");
        auto run = [&](const char* mode, bool reject = false) {
            bool rejected = false;
            try {
                auto files = run_import_process(executable, root,
                                                {{{"mode", mode}, {"control", path_utf8(control)}},
                                                 {{"part.bin", {std::byte{7}}}}},
                                                {});
                check(files.size() == 1 && files[0].name == "result.bin" &&
                          files[0].bytes == std::vector{std::byte{42}},
                      "Import completion changed output");
            } catch (const std::exception& error) {
                if (!reject)
                    throw;
                if (std::string_view(mode) == "fail") {
                    const std::string_view message(error.what());
                    check(message.find("exit 7") != message.npos &&
                              message.find("fixture.failed: expected worker diagnostic") !=
                                  message.npos &&
                              message.find("last worker checkpoint: fixture pending entry") !=
                                  message.npos,
                          "Worker checkpoint replaced the exit/error diagnostic or was lost");
                } else if (std::string_view(mode) == "partial-status") {
                    const std::string_view message(error.what());
                    check(message.find("exit 7") != message.npos &&
                              message.find("last worker checkpoint") == message.npos,
                          "Partial worker checkpoint obscured the supervisor failure");
                }
                rejected = true;
            }
            check(rejected == reject, "Import failure was not reported");
            return std::filesystem::u8path(read(control / "stage"));
        };
        if (std::string_view(argv[2]) == "ordinary") {
            for (const auto mode : {"normal", "fail", "partial-status"}) {
                const auto stage = run(mode, std::string_view(mode) != "normal");
                check(!std::filesystem::exists(stage), "Retired recognized stage was retained");
            }
            const auto stage = run("unknown");
            check(std::filesystem::is_regular_file(stage / "user-notes.txt") &&
                      std::filesystem::is_regular_file(stage / "input/part.bin") &&
                      std::filesystem::is_regular_file(stage / "output/result.bin"),
                  "Producer cleanup erased unrecognized stage contents");
            const auto result = cleanup_worker_stages(lease);
            check(result.size() == 1 && result[0].at("removed") == false,
                  "Maintenance disagreed with producer retention");
            std::filesystem::remove(stage / "user-notes.txt");
            check(cleanup_worker_stages(lease)[0].at("removed") == true,
                  "Recognized repaired stage was not reclaimed");
        }
#ifdef __linux__
        else if (std::string_view(argv[2]) == "escaped") {
            Descendant descendant(control);
            const auto stage = run("escaped");
            check(std::filesystem::is_regular_file(stage / "owner.lock") &&
                      std::filesystem::is_regular_file(stage / "input/part.bin") &&
                      std::filesystem::is_regular_file(stage / "output/result.bin"),
                  "Producer cleanup erased a stage still owned by a live descendant");
            check(!WorkerStageLease::try_open(stage / "owner.lock"),
                  "Live descendant lost staging ownership");
            const auto result = cleanup_worker_stages(lease);
            check(result.size() == 1 && result[0].at("removed") == false,
                  "Maintenance removed a live stage");
            descendant.finish();
            check(cleanup_worker_stages(lease)[0].at("removed") == true,
                  "Retired descendant stage was not reclaimed");
        }
#endif
        else
            throw std::runtime_error("Unknown import fixture mode");
        check(read(root / "source.txt") == "saved source" && cleanup_worker_stages(lease).empty(),
              "Cleanup changed saved source or left recognized staging");
        std::cout << "Import producer and maintenance ownership checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
