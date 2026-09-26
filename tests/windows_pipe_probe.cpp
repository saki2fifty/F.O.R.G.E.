// Windows anonymous-pipe probe. Pins the actual WriteFile/ReadFile error
// contract on a nonblocking pipe and exercises `forge::RuntimeIo`'s real
// send/flush path so the windows83 SDK workflow stage2 snapshot-timeout
// review has an executable answer for the `runtime_io.hpp`
// `if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE)` branch.
//
// Scope: API-contract observation of anonymous-pipe WriteFile/ReadFile
// (scenarios A, B) and a real production-path exercise of
// `forge::RuntimeIo::send` / `flush` against a payload larger than the
// default anonymous-pipe quota (scenario C). The probe does NOT claim
// to reproduce the windows83 fixture failure; that failure depends on
// the editor's GPU swap chain and `GameSession::poll_candidate`,
// neither of which this probe exercises.
//
// Build with MSVC (matches the pinned setup used by editor-audit and
// standalone-audit workflows):
//   cl /nologo /std:c++20 /EHsc /W4 /O2 /Fe:probe.exe \
//       tests\windows_pipe_probe.cpp
//
// Run:
//   probe.exe
//
// Output is a JSON document on stdout. The process exits with a
// non-zero status only when scenario C observes an exception, an
// early `io.closed()`, or a data-integrity mismatch; in those cases
// the JSON evidence is still emitted so the manager can see what was
// observed. Scenarios A and B are pure observations and never fail
// the process.

#include "../src/runtime_io.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

struct Call {
    std::uint32_t iteration;
    bool ok;
    DWORD result;
    DWORD last_error;
};

struct FlushStep {
    std::uint32_t iteration;
    bool pending_before;
    bool pending_after;
    bool closed_after;
};

std::vector<Call> fill_pipe(HANDLE write_handle, std::size_t chunk_bytes, std::uint32_t max_calls) {
    std::vector<char> payload(chunk_bytes, 'y');
    std::vector<Call> calls;
    calls.reserve(max_calls);
    for (std::uint32_t i = 0; i < max_calls; ++i) {
        DWORD written = 0;
        BOOL ok = WriteFile(write_handle, payload.data(), static_cast<DWORD>(payload.size()),
                            &written, nullptr);
        DWORD err = ok ? 0u : GetLastError();
        calls.push_back({i, ok != FALSE, written, err});
        if (!ok)
            break;
        if (written != payload.size())
            break;
    }
    return calls;
}

std::vector<Call> drain_pipe(HANDLE read_handle, std::size_t buffer_bytes,
                             std::uint32_t max_calls) {
    std::vector<char> buf(buffer_bytes);
    std::vector<Call> calls;
    calls.reserve(max_calls);
    for (std::uint32_t i = 0; i < max_calls; ++i) {
        DWORD read = 0;
        BOOL ok = ReadFile(read_handle, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
        DWORD err = ok ? 0u : GetLastError();
        calls.push_back({i, ok != FALSE, read, err});
        if (!ok || read == 0)
            break;
    }
    return calls;
}

std::string calls_json(const std::vector<Call>& calls) {
    std::string s = "[";
    for (std::size_t i = 0; i < calls.size(); ++i) {
        char line[160];
        std::snprintf(line, sizeof(line),
                      "%s{\"iteration\":%u,\"ok\":%s,\"result\":%lu,\"last_error\":%lu}",
                      i == 0 ? "" : ",", calls[i].iteration, calls[i].ok ? "true" : "false",
                      static_cast<unsigned long>(calls[i].result),
                      static_cast<unsigned long>(calls[i].last_error));
        s += line;
    }
    s += "]";
    return s;
}

std::string flush_steps_json(const std::vector<FlushStep>& steps) {
    std::string s = "[";
    for (std::size_t i = 0; i < steps.size(); ++i) {
        char line[160];
        std::snprintf(
            line, sizeof(line),
            "%s{\"iteration\":%u,\"pending_before\":%s,"
            "\"pending_after\":%s,\"closed_after\":%s}",
            i == 0 ? "" : ",", steps[i].iteration, steps[i].pending_before ? "true" : "false",
            steps[i].pending_after ? "true" : "false", steps[i].closed_after ? "true" : "false");
        s += line;
    }
    s += "]";
    return s;
}

std::string error_name(DWORD err) {
    switch (err) {
    case 0:
        return "OK";
    case ERROR_NO_DATA:
        return "ERROR_NO_DATA";
    case ERROR_BROKEN_PIPE:
        return "ERROR_BROKEN_PIPE";
    case ERROR_PIPE_BUSY:
        return "ERROR_PIPE_BUSY";
    case ERROR_PIPE_CONNECTED:
        return "ERROR_PIPE_CONNECTED";
    case ERROR_NOT_ENOUGH_QUOTA:
        return "ERROR_NOT_ENOUGH_QUOTA";
    case ERROR_IO_PENDING:
        return "ERROR_IO_PENDING";
    case ERROR_IO_INCOMPLETE:
        return "ERROR_IO_INCOMPLETE";
    case ERROR_HANDLE_EOF:
        return "ERROR_HANDLE_EOF";
    case ERROR_INVALID_HANDLE:
        return "ERROR_INVALID_HANDLE";
    default: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "ERR_%lu", static_cast<unsigned long>(err));
        return buf;
    }
    }
}

struct Summary {
    std::uint32_t total_calls;
    std::uint32_t success_count;
    std::uint32_t partial_success_count;
    std::uint32_t zero_success_count;
    std::uint32_t false_count;
    std::string first_false_error_name;
    DWORD first_false_error_value;
};

Summary summarize(const std::vector<Call>& calls, std::size_t requested) {
    Summary s{};
    s.total_calls = static_cast<std::uint32_t>(calls.size());
    for (const auto& c : calls) {
        if (c.ok) {
            if (c.result == requested)
                s.success_count++;
            else if (c.result == 0)
                s.zero_success_count++;
            else
                s.partial_success_count++;
        } else {
            s.false_count++;
            if (s.first_false_error_name.empty()) {
                s.first_false_error_name = error_name(c.last_error);
                s.first_false_error_value = c.last_error;
            }
        }
    }
    return s;
}

std::string summary_json(const Summary& s) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"total_calls\":%u,\"full_success\":%u,"
                  "\"partial_success\":%u,\"zero_success\":%u,"
                  "\"false_return\":%u,"
                  "\"first_false_error\":\"%s\","
                  "\"first_false_error_value\":%lu}",
                  s.total_calls, s.success_count, s.partial_success_count, s.zero_success_count,
                  s.false_count,
                  s.first_false_error_name.empty() ? "" : s.first_false_error_name.c_str(),
                  static_cast<unsigned long>(s.first_false_error_value));
    return buf;
}

} // namespace

int wmain() {
    constexpr std::size_t kChunkBytes = 1024;
    constexpr std::size_t kReadBufferBytes = 8192;
    constexpr std::uint32_t kMaxCalls = 4096;
    constexpr std::size_t kRuntimeIoPayloadBytes = 64 * 1024 + 4096;
    constexpr std::uint32_t kMaxFlushes = 32;

    std::vector<std::string> scenarios;
    int exit_status = 0;

    // ---- Scenario A: fill, then drain -----------------------------------
    {
        HANDLE read_h = nullptr;
        HANDLE write_h = nullptr;
        if (!CreatePipe(&read_h, &write_h, nullptr, 0)) {
            std::fprintf(stderr, "A: CreatePipe failed: %lu\n", GetLastError());
            return 1;
        }
        DWORD mode = PIPE_NOWAIT;
        if (!SetNamedPipeHandleState(read_h, &mode, nullptr, nullptr) ||
            !SetNamedPipeHandleState(write_h, &mode, nullptr, nullptr)) {
            std::fprintf(stderr, "A: SetNamedPipeHandleState failed: %lu\n", GetLastError());
            return 1;
        }

        auto writes = fill_pipe(write_h, kChunkBytes, kMaxCalls);
        auto reads = drain_pipe(read_h, kReadBufferBytes, kMaxCalls);

        Summary sw = summarize(writes, kChunkBytes);
        Summary sr = summarize(reads, kReadBufferBytes);

        scenarios.push_back("\"scenario_a_fill_then_drain\": {\"calls\": {\"writes\": " +
                            calls_json(writes) + ",\"reads\": " + calls_json(reads) +
                            "}, \"summary\": {\"writes\": " + summary_json(sw) +
                            ",\"reads\": " + summary_json(sr) + "}}");

        CloseHandle(read_h);
        CloseHandle(write_h);
    }

    // ---- Scenario B: write into a pipe whose read end is closed --------
    {
        HANDLE read_h = nullptr;
        HANDLE write_h = nullptr;
        if (!CreatePipe(&read_h, &write_h, nullptr, 0)) {
            std::fprintf(stderr, "B: CreatePipe failed: %lu\n", GetLastError());
            return 1;
        }
        DWORD mode = PIPE_NOWAIT;
        if (!SetNamedPipeHandleState(write_h, &mode, nullptr, nullptr)) {
            std::fprintf(stderr, "B: SetNamedPipeHandleState failed: %lu\n", GetLastError());
            return 1;
        }
        CloseHandle(read_h);

        auto writes = fill_pipe(write_h, kChunkBytes, 8);
        Summary sw = summarize(writes, kChunkBytes);

        scenarios.push_back("\"scenario_b_disconnected_reader\": {\"calls\": " +
                            calls_json(writes) + ", \"summary\": " + summary_json(sw) + "}");

        CloseHandle(write_h);
    }

    // ---- Scenario C: real RuntimeIo send/flush path ---------------------
    //   Mirrors the SDK runtime's actual call shape (see
    //   src/sdk_play_runtime.cpp:1284-1343): the caller appends "\n"
    //   to response.dump() and passes the result to io.send(); the
    //   runtime's process loop then calls io.flush() until the
    //   response is drained. When flush() stalls because the pipe is
    //   full (the editor's pump is not running in the real fixture),
    //   the probe drains the pipe itself and retries flush() in a
    //   bounded loop. Compares collected bytes with the sent payload
    //   (including the trailing "\n") for data integrity. Fails the
    //   process if any of:
    //     - RuntimeIo throws
    //     - io.closed() flips before the payload is fully drained
    //     - the bytes drained do not match the payload byte-for-byte
    {
        const HANDLE saved_in = GetStdHandle(STD_INPUT_HANDLE);
        const HANDLE saved_out = GetStdHandle(STD_OUTPUT_HANDLE);

        HANDLE pipe_read = nullptr;
        HANDLE pipe_write = nullptr;
        if (!CreatePipe(&pipe_read, &pipe_write, nullptr, 0)) {
            std::fprintf(stderr, "C: CreatePipe failed: %lu\n", GetLastError());
            return 1;
        }
        HANDLE runtime_stdin = nullptr;
        HANDLE runtime_stdout = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), pipe_read, GetCurrentProcess(), &runtime_stdin, 0,
                             FALSE, DUPLICATE_SAME_ACCESS) ||
            !DuplicateHandle(GetCurrentProcess(), pipe_write, GetCurrentProcess(), &runtime_stdout,
                             0, FALSE, DUPLICATE_SAME_ACCESS)) {
            std::fprintf(stderr, "C: DuplicateHandle failed: %lu\n", GetLastError());
            return 1;
        }
        DWORD mode = PIPE_NOWAIT;
        SetNamedPipeHandleState(runtime_stdin, &mode, nullptr, nullptr);
        SetNamedPipeHandleState(runtime_stdout, &mode, nullptr, nullptr);
        SetStdHandle(STD_INPUT_HANDLE, runtime_stdin);
        SetStdHandle(STD_OUTPUT_HANDLE, runtime_stdout);

        std::vector<FlushStep> flush_steps;
        std::vector<Call> read_calls;
        std::string collected;
        bool closed_early = false;
        bool data_mismatch = false;
        bool threw = false;
        std::string what;

        try {
            forge::RuntimeIo io;
            const std::string payload(kRuntimeIoPayloadBytes, 'z');
            // SDK callers (src/sdk_play_runtime.cpp:1321) build
            // response.dump() + "\n" and pass that whole string to
            // io.send(). The probe does the same.
            const std::string expected = payload + "\n";
            io.send(expected);

            // Bounded loop: flush if there is pending output, then drain
            // whatever the reader has. `io.pending() == false` does NOT
            // mean the pipe is drained — the runtime may have flushed
            // everything to the pipe, with the last bytes still sitting
            // in the OS buffer waiting to be read. The loop continues
            // until `collected.size() == expected.size()` (success), or
            // the reader fails, or the budget runs out.
            std::vector<char> buf(kReadBufferBytes);
            std::uint32_t rounds = 0;
            const std::uint32_t kRounds = 64;
            while (collected.size() < expected.size() && rounds < kRounds) {
                ++rounds;
                if (io.pending() && !io.closed()) {
                    const bool pending_before = io.pending();
                    io.flush();
                    flush_steps.push_back({static_cast<std::uint32_t>(flush_steps.size()),
                                           pending_before, io.pending(), io.closed()});
                    if (io.closed()) {
                        closed_early = true;
                        break;
                    }
                }
                DWORD read = 0;
                BOOL ok =
                    ReadFile(pipe_read, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
                DWORD err = ok ? 0u : GetLastError();
                read_calls.push_back(
                    {static_cast<std::uint32_t>(read_calls.size()), ok != FALSE, read, err});
                if (!ok)
                    break;
                if (read > 0)
                    collected.append(buf.data(), read);
            }

            data_mismatch = (collected != expected);
        } catch (const std::exception& e) {
            threw = true;
            what = e.what();
        }

        // Restore originals and close duplicates. The restore runs
        // whether or not the try block threw, so the probe process
        // keeps a usable stdout for the final JSON print below.
        SetStdHandle(STD_INPUT_HANDLE, saved_in);
        SetStdHandle(STD_OUTPUT_HANDLE, saved_out);
        CloseHandle(runtime_stdin);
        CloseHandle(runtime_stdout);
        CloseHandle(pipe_read);
        CloseHandle(pipe_write);

        char summary_buf[768];
        std::snprintf(summary_buf, sizeof(summary_buf),
                      "{\"threw\":%s,\"closed_early\":%s,\"data_mismatch\":%s,"
                      "\"collected_bytes\":%zu,\"expected_bytes\":%zu,"
                      "\"exception_message\":\"%s\"}",
                      threw ? "true" : "false", closed_early ? "true" : "false",
                      data_mismatch ? "true" : "false", collected.size(),
                      kRuntimeIoPayloadBytes + 1, threw ? what.c_str() : "");

        scenarios.push_back("\"scenario_c_runtimeio_send_flush\": {\"flush_steps\": " +
                            flush_steps_json(flush_steps) + ", \"read_calls\": " +
                            calls_json(read_calls) + ", \"summary\": " + summary_buf + "}");

        if (threw || closed_early || data_mismatch)
            exit_status = 2;
    }

    // ---- Output JSON ----------------------------------------------------
    std::printf("{\n");
    std::printf("  \"probe\": \"FORGE Windows pipe contract probe\",\n");
    std::printf("  \"scope\": \"API contract observation + RuntimeIo send/flush path; does not"
                " reproduce the windows83 SDK workflow fixture failure\",\n");
    std::printf("  \"constants\": {\"chunk_bytes\": %zu, \"read_buffer_bytes\": %zu, "
                "\"max_calls\": %u, \"runtimeio_payload_bytes\": %zu, \"max_flushes\": %u},\n",
                kChunkBytes, kReadBufferBytes, kMaxCalls, kRuntimeIoPayloadBytes, kMaxFlushes);
    std::printf("  \"error_no_data\": %d,\n", static_cast<int>(ERROR_NO_DATA));
    std::printf("  \"error_broken_pipe\": %d,\n", static_cast<int>(ERROR_BROKEN_PIPE));
    std::printf("  \"error_pipe_busy\": %d,\n", static_cast<int>(ERROR_PIPE_BUSY));
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        std::printf("  %s%s\n", scenarios[i].c_str(), i + 1 == scenarios.size() ? "" : ",");
    }
    std::printf("}\n");
    return exit_status;
}
