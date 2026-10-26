#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#ifdef __linux__
#include <csignal>
#include <sys/prctl.h>
#endif
#endif
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--unrelated-stage") {
        const auto stage = std::filesystem::u8path(argv[2]);
        std::ofstream(stage / "started") << "ready";
        for (unsigned i = 0; i < 400; ++i) {
            if (std::filesystem::exists(stage / "continue.request"))
                return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 23;
    }
    const bool flat_worker = (argc == 2 && (std::string(argv[1]) == "--build-navigation" ||
                                            std::string(argv[1]) == "--script-worker" ||
                                            std::string(argv[1]) == "--inspect-sdk-worker" ||
                                            std::string(argv[1]) == "--inspect-ui-worker")) ||
                             (argc == 3 && std::string(argv[1]) == "--file=source.gltf" &&
                              std::string(argv[2]) == "--config_file=config.json");
    if (flat_worker) {
        std::ofstream("started.txt") << "started";
        std::string mode;
        std::ifstream("request.txt") >> mode;
        if (mode == "nested") {
            std::filesystem::create_directory("nested");
            for (unsigned i = 0; i < 3; ++i)
                std::ofstream("nested/file" + std::to_string(i)) << std::string(3000, 'x');
        } else if (mode == "ordinary") {
            std::ofstream("result.txt") << "ordinary flat result";
        } else
            return 11;
        return 0;
    }
    if (argc != 2 || std::string(argv[1]) != "--build-asset")
        return 1;
    std::string mode;
    std::ifstream("request.txt") >> mode;
    if (mode == "lease") {
        // The parent releases its copy and verifies the running child still
        // excludes a new lease; no parent-local handle value is used here.
        std::ofstream("output/started") << "ready";
        for (unsigned i = 0; i < 400; ++i) {
            if (std::filesystem::exists("continue.request"))
                return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 22;
    }
    if (mode == "limits") {
#ifdef _WIN32
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &info,
                                       sizeof(info), nullptr))
            return 2;
        if (info.ProcessMemoryLimit != 128ull * 1024 * 1024 ||
            info.BasicLimitInformation.ActiveProcessLimit != 1)
            return 3;
#else
        rlimit memory{}, file{}, cpu{};
        if (getrlimit(RLIMIT_AS, &memory) || getrlimit(RLIMIT_FSIZE, &file) ||
            getrlimit(RLIMIT_CPU, &cpu))
            return 2;
        if (memory.rlim_cur != 128ull * 1024 * 1024 || file.rlim_cur != 4096 || cpu.rlim_cur != 2)
            return 3;
#endif
#ifdef __linux__
        int signal = 0;
        if (prctl(PR_GET_PDEATHSIG, &signal, 0L, 0L, 0L) || signal != SIGKILL)
            return 4;
#endif
        std::ofstream("output/valid.bin") << "validated resource policy";
        return 0;
    }
    if (mode == "atomic-rename") {
        std::ofstream("output/first.tmp") << "complete output";
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < end) {
            std::filesystem::rename("output/first.tmp", "output/second.tmp");
            std::filesystem::rename("output/second.tmp", "output/first.tmp");
        }
        std::filesystem::rename("output/first.tmp", "output/valid.bin");
        return 0;
    }
    if (mode == "fail")
        return 7;
    if (mode == "file") {
        std::ofstream("output/large.bin").put('x');
        std::error_code error;
        std::filesystem::resize_file("output/large.bin", 4097, error);
        return error ? 8 : 0;
    }
    if (mode == "total") {
        for (unsigned i = 0; i < 3; ++i) {
            std::ofstream out("output/file" + std::to_string(i));
            out << std::string(3000, 'x');
        }
        return 0;
    }
    if (mode == "count") {
        for (unsigned i = 0; i < 9; ++i)
            std::ofstream("output/file" + std::to_string(i)).put('x');
        return 0;
    }
    if (mode == "nested") {
        std::filesystem::create_directory("output/unexpected");
        return 0;
    }
    if (mode == "cooperate") {
        for (unsigned i = 0; i < 500; ++i) {
            if (std::filesystem::exists("cancel.request")) {
                std::ofstream("output/cancelled.txt") << "stopped";
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 9;
    }
    if (mode == "delay") {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 0;
    }
    return 10;
}
