#pragma once
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif
// Test-only crash capture. The fixture may exit 0xC0000005 (Windows
// access violation) before any C++ exception handler runs. Capture
// the original fault record through the existing fixture boundary.
// Compiled into fixture binaries only; the shipped
// editor is unaffected. Best-effort first evidence: exception record
// + module base + relative fault offset + last breadcrumb. No
// CaptureStackBackTrace (it would only see the filter's stack, not
// ep->ContextRecord's fault stack), no symbol resolution, no heap
// allocation inside the filter, no extra link dependencies.
namespace forge::test {
inline char fixture_last_breadcrumb[64] = "(none)";
inline void fixture_breadcrumb(const char* name) {
    std::fprintf(stdout, "FORGE fixture breadcrumb %s t=%llu\n", name,
                 static_cast<unsigned long long>(SDL_GetTicks()));
    std::fflush(stdout);
    const size_t len = std::strlen(name);
    if (len + 1 < sizeof(fixture_last_breadcrumb)) {
        std::memcpy(fixture_last_breadcrumb, name, len + 1);
    }
}
#ifdef _WIN32
inline wchar_t fixture_crash_path_buffer[MAX_PATH] = {};
inline HANDLE fixture_crash_file_handle = nullptr;
inline LONG WINAPI fixture_crash_filter(EXCEPTION_POINTERS* ep) {
    // Best-effort. Uses a pre-opened handle + pre-formatted path
    // prepared at normal startup; performs no std::filesystem::path
    // work, no create_directories, no wstring conversion, no heap
    // allocation inside the filter. Returns
    // EXCEPTION_EXECUTE_HANDLER so the CRT still terminates with
    // the original exit code. Acceptance assertions unchanged.
    if (fixture_crash_file_handle == nullptr || fixture_crash_file_handle == INVALID_HANDLE_VALUE)
        return EXCEPTION_EXECUTE_HANDLER;
    char buffer[768];
    DWORD written = 0;
    const DWORD_PTR fault = reinterpret_cast<DWORD_PTR>(ep->ExceptionRecord->ExceptionAddress);
    HMODULE module = nullptr;
    char module_path[MAX_PATH] = "(unknown)";
    DWORD_PTR module_base = 0;
    DWORD_PTR offset = fault;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(ep->ExceptionRecord->ExceptionAddress), &module) &&
        module != nullptr) {
        module_base = reinterpret_cast<DWORD_PTR>(module);
        if (fault >= module_base)
            offset = fault - module_base;
        GetModuleFileNameA(module, module_path, MAX_PATH);
    }
    std::snprintf(buffer, sizeof(buffer),
                  "ExceptionCode=0x%08lX FaultAddress=0x%p Module=%s "
                  "Base=0x%p Offset=0x%llx LastBreadcrumb=%s\n",
                  static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                  reinterpret_cast<void*>(fault), module_path, reinterpret_cast<void*>(module_base),
                  static_cast<unsigned long long>(offset), fixture_last_breadcrumb);
    WriteFile(fixture_crash_file_handle, buffer, static_cast<DWORD>(std::strlen(buffer)), &written,
              nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void initialize_fixture_crash_capture(const std::filesystem::path& directory) {
    try {
        const auto path = std::filesystem::absolute(directory) / "fixture-crash.txt";
        std::filesystem::create_directories(path.parent_path());
        const auto wide = path.wstring();
        if (wide.size() < MAX_PATH) {
            std::memcpy(fixture_crash_path_buffer, wide.c_str(),
                        (wide.size() + 1) * sizeof(wchar_t));
            fixture_crash_file_handle =
                CreateFileW(fixture_crash_path_buffer, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fixture_crash_file_handle == INVALID_HANDLE_VALUE)
                fixture_crash_file_handle = nullptr;
        }
    } catch (...) {
        fixture_crash_file_handle = nullptr;
    }
    SetUnhandledExceptionFilter(&fixture_crash_filter);
}
#endif
} // namespace forge::test
