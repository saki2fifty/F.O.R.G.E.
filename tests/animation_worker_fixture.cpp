// Bounded child used only to verify cancellation, failure and resource policy.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#endif
int main() {
    std::string mode;
    std::ifstream("source.gltf") >> mode;
    if (mode == "fail")
        return 7;
    if (mode == "limits") {
#ifdef _WIN32
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &info,
                                       sizeof(info), nullptr))
            return 2;
        return info.ProcessMemoryLimit == 512ull * 1024 * 1024 &&
                       info.BasicLimitInformation.ActiveProcessLimit == 1
                   ? 0
                   : 3;
#else
        rlimit memory{}, file{};
        if (getrlimit(RLIMIT_AS, &memory) || getrlimit(RLIMIT_FSIZE, &file))
            return 2;
        return memory.rlim_cur == 512ull * 1024 * 1024 && file.rlim_cur == 16ull * 1024 * 1024 ? 0
                                                                                               : 3;
#endif
    }
    if (mode == "size") {
        std::ofstream("oversize.bin").put('x');
        std::error_code ec;
        std::filesystem::resize_file("oversize.bin", 16ull * 1024 * 1024 + 1, ec);
        return ec ? 4 : 0;
    }
    if (mode == "delay") {
        std::this_thread::sleep_for(std::chrono::seconds(45));
        return 0;
    }
    return 5;
}
