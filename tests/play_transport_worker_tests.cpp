// Real-process regression test for forge::PlayTransportWorker +
// forge::PlaySession.
//
// Each scenario is a C++ child spawned via
// SDL_CreateProcessWithProperties pointing at the test executable
// path (argv[0] / the OS self-path). The child detects its scenario
// name in argv[1] + "--child" sentinel and runs that scenario's
// protocol using plain C stdio (printf / fgets / fwrite / fflush)
// so the same source compiles on every platform without per-
// platform branching. NO /dev/stdin, /dev/stdout, /proc/self/exe,
// fork, or unrelated sleep sentinel.
//
// The PARENT never touches the child's stdin / stdout / stderr
// directly — it goes through worker_.submit() / drain APIs or the
// production PlaySession::pump() path. Windows binary mode is set
// inside the child so newline-terminated JSON is framed correctly
// (text-mode stdout would expand \n to \r\n and break framing).
//
// Required scenarios (rev 4 explicit-correction contract):
//   A. Large response (> 69 KiB) while main sleeps > 5 s. Exact
//      bytes + on-time receipt survive; no timeout.
//   B. Child never reads stdin but stays alive. Deadline fires
//      within the bound; stop/join returns; no spinning.
//   C. Child reads request but never answers. AwaitingResponse
//      deadline fires AFTER write complete.
//   D. Child responds after deadline. Production worker rejects
//      the late response and reports a timeout.
//   E. Partial / fragmented newline framing, oversize/no newline,
//      stderr flood, malformed JSON. Worker handles byte bounds;
//      main JSON validation handles malformed protocol.
//   F. Complete final Quit + process exit preserved; EOF while
//      child alive rejected.
//   G. Stop while IO pending; restart SAME worker object. Old
//      partial line / receipt / error cannot reach new session.
//      Submit before start / after stop rejected.
//   H. Existing sdk_play_editor_transport_tests (wrong-id / stale
//      / protocol) remain unchanged — NOT replaced by this file.
//   I. Reply then crash (exit 137). Worker surfaces a terminal
//      process-end failure (exited OR stdout-closed EOF, OS-
//      dependent ordering) AFTER publishing the receipt via the
//      bounded final-reply drain.
//   J. mailbox_busy_ rejects second submit while a request is
//      queued / writing / awaiting / receipt-ready.
//   K. Oversize response (17 MiB, no newline) + immediate child
//      exit. Shared consume helper is invoked by both the regular
//      read phase AND the bounded final-reply drain; whichever
//      path catches the buffered bytes past the 16 MiB cap must
//      trip the cap failure and drop the data without copying it
//      into a receipt.

#include "play.hpp"
#include "play_transport_worker.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
// Test-failure exception. The top-level main() catches this,
// cleans up any worker/child state via the scenario guard, and
// exits with non-zero. Throwing instead of std::exit ensures
// RAII destructors run and child processes are reaped before
// the test binary terminates (no orphan child left alive on
// assertion failure).
struct TestFailure : public std::runtime_error {
    explicit TestFailure(const std::string& m) : std::runtime_error(m) {}
};
void require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "[fail] " << message << std::endl;
        throw TestFailure(message);
    }
}
void log(const std::string& msg) { std::cerr << "[info] " << msg << std::endl; }

#if defined(_WIN32)
extern "C" int _setmode(int, int);
#define BINARY_MODE(fd) _setmode(fd, 0x8000)
#else
#define BINARY_MODE(fd) ((void)0)
#endif

const char* self_executable_path(const char* argv0) {
    static const char* cached = nullptr;
    if (cached)
        return cached;
    // Use argv[0] directly on every platform. On Windows the
    // caller passes an absolute path; on Linux/macOS argv[0] is
    // the path the parent used to spawn the child (typically
    // absolute after the parent's `SDL_GetBasePath` lookup). No
    // /proc/self/exe, no /dev/stdin, no fork.
    cached = argv0 ? argv0 : "forge_play_transport_worker_tests";
    return cached;
}

struct ChildHandles {
    SDL_Process* process = nullptr;
    SDL_IOStream* stdin_pipe = nullptr;
    SDL_IOStream* stdout_pipe = nullptr;
    SDL_IOStream* stderr_pipe = nullptr;
};
ChildHandles spawn_child(const char* argv0, const std::string& scenario) {
    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0)
        throw TestFailure("SDL_CreateProperties failed");
    std::string argv0_str = self_executable_path(argv0);
    std::string scen = scenario;
    std::string sentinel = "--child";
    std::vector<char*> args;
    args.push_back(const_cast<char*>(argv0_str.c_str()));
    args.push_back(const_cast<char*>(scen.c_str()));
    args.push_back(const_cast<char*>(sentinel.c_str()));
    args.push_back(nullptr);
    // Property failures: clean up `props` (no process yet) and
    // throw. require_prop itself throws on failure, so the
    // cleanup runs before the throw escapes the lambda.
    auto require_prop = [&](bool ok, const char* what) {
        if (!ok) {
            SDL_DestroyProperties(props);
            throw TestFailure(std::string(what) + " for " + scenario);
        }
    };
    require_prop(SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data()),
                 "SDL_SetPointerProperty ARGS failed");
    require_prop(
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP),
        "SDL_SetNumberProperty STDIN failed");
    require_prop(
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP),
        "SDL_SetNumberProperty STDOUT failed");
    require_prop(
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP),
        "SDL_SetNumberProperty STDERR failed");
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (p == nullptr) {
        throw TestFailure(std::string("SDL_CreateProcessWithProperties failed for ") + scenario +
                          ": " + SDL_GetError());
    }
    ChildHandles c;
    c.process = p;
    c.stdin_pipe = SDL_GetProcessInput(p);
    c.stdout_pipe = SDL_GetProcessOutput(p);
    c.stderr_pipe = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
        SDL_GetProcessProperties(p), SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
    if (c.stdin_pipe == nullptr || c.stdout_pipe == nullptr || c.stderr_pipe == nullptr) {
        // Stream acquisition failed. Clean up the process so we
        // do not leak. Caller never sees this handle.
        SDL_KillProcess(p, true);
        SDL_WaitProcess(p, true, nullptr);
        SDL_DestroyProcess(p);
        throw TestFailure(std::string("SDL_GetProcess* returned null for ") + scenario);
    }
    return c;
}

// RAII guard for a SINGLE spawned SDL_Process* BEFORE ownership
// transfers to PlayTransportWorker::start(). On scope exit, if
// `child` is non-null, the guard kills/waits/destroys the
// process. If start() succeeds, the caller MUST set `child =
// nullptr` immediately so the guard does nothing and the worker
// owns the process. The worker's destructor handles cleanup
// post-transfer. Never call reap_child or SDL_DestroyProcess on
// `c.process` after the release point — that would UAF the
// worker's internal handle.
struct ChildGuard {
    SDL_Process* child = nullptr;
    ~ChildGuard() {
        if (child) {
            SDL_KillProcess(child, true);
            SDL_WaitProcess(child, true, nullptr);
            SDL_DestroyProcess(child);
            child = nullptr;
        }
    }
};

// Bounded wait until `pred()` returns true OR `timeout_ms` elapses.
template <typename Pred> bool wait_for(Pred pred, int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  start)
                .count() >= timeout_ms)
            return pred();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}
} // namespace

// -- Child-side scenarios ----------------------------------------------
// Each scenario uses plain C stdio on stdin/stdout/stderr so it
// compiles without per-platform branching.

namespace child {
int run(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[2], "--child") != 0)
        return 0;
    BINARY_MODE(0);
    BINARY_MODE(1);
    BINARY_MODE(2);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stdin, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    const std::string scenario = argv[1];

    if (scenario == "delayed_main") {
        for (int i = 1; i <= 3; ++i) {
            fprintf(stdout, "{\"protocol\":2,\"id\":%d,\"ok\":true,\"line\":%d}\n", i, i);
            SDL_Delay(20);
        }
        return 0;
    }
    // A: Large response (≥ 69 KiB). Writes a 70 KiB response in
    // 1 KiB chunks so the pipe fills repeatedly, then stays
    // alive for a bounded wait so the worker does NOT see EOF
    // before the parent drains the receipt. The bounded alive
    // wait is the production-equivalent of "child waits for next
    // request": the runtime normally stays alive across many
    // snapshot polls; a child that exits immediately after
    // sending a response would force an unrelated EOF failure.
    if (scenario == "large_response") {
        // Read and discard parent's request.
        char buf[256];
        if (std::fgets(buf, sizeof(buf), stdin) == nullptr)
            return 0;
        // Write a 70 KiB response.
        std::string header = "{\"protocol\":2,\"id\":1,\"ok\":true,\"data\":\"";
        std::string footer = "\"}\n";
        // Framing math: total bytes on wire = 70 * 1024 (71680).
        // Receipt (newline stripped) = total - 1 = 71679.
        const std::size_t payload_size = 70 * 1024 - header.size() - footer.size();
        const std::size_t total_wire_bytes = header.size() + payload_size + footer.size();
        std::fwrite(header.data(), 1, header.size(), stdout);
        std::string payload(payload_size, 'X');
        std::size_t off = 0;
        while (off < payload.size()) {
            const std::size_t chunk = std::min<std::size_t>(1024, payload.size() - off);
            const std::size_t n = std::fwrite(payload.data() + off, 1, chunk, stdout);
            if (n > 0)
                off += n;
            else
                SDL_Delay(1);
        }
        std::fwrite(footer.data(), 1, footer.size(), stdout);
        std::fflush(stdout);
        // Documented invariant for the parent assertion.
        (void)total_wire_bytes; // 70 * 1024 = 71680
        // Stay alive long enough for the parent to drain the
        // receipt and assert exact bytes. Bounded 10 s.
        for (int i = 0; i < 10; ++i)
            SDL_Delay(1000);
        return 0;
    }
    // B: Stay alive, never read stdin. Large parent request
    // eventually fills the pipe and the worker deadline fires.
    if (scenario == "silent_consumer") {
        // Do nothing with stdin; stay alive until killed.
        for (;;)
            SDL_Delay(1000);
    }
    // C: Reads request, never answers. Worker stays in
    // AwaitingResponse and deadline fires AFTER write complete.
    if (scenario == "read_no_answer") {
        char buf[4096];
        if (std::fgets(buf, sizeof(buf), stdin) == nullptr)
            return 0;
        for (;;)
            SDL_Delay(1000);
    }
    // D: Reads request, sleeps > 5 s, then answers.
    if (scenario == "late_answer") {
        char buf[4096];
        if (std::fgets(buf, sizeof(buf), stdin) == nullptr)
            return 0;
        SDL_Delay(6000);
        if (std::fprintf(stdout, "{\"protocol\":2,\"id\":1,\"ok\":true,\"late\":true}\n") < 0)
            return 0;
        std::fflush(stdout);
        return 0;
    }
    // E1: Partial newline framing — write partial, wait, complete.
    if (scenario == "partial_then_complete") {
        fputs("{\"protocol\":2,\"id\":1,\"ok\":", stdout);
        SDL_Delay(50);
        fputs("true}\n", stdout);
        SDL_Delay(50);
        return 0;
    }
    // E2: Oversize — 17 MiB without a newline.
    if (scenario == "oversize_no_newline") {
        std::vector<char> burst(17 * 1024 * 1024, 'X');
        std::size_t off = 0;
        while (off < burst.size()) {
            const std::size_t chunk = std::min<std::size_t>(64 * 1024, burst.size() - off);
            const std::size_t n = std::fwrite(burst.data() + off, 1, chunk, stdout);
            if (n > 0)
                off += n;
            else
                SDL_Delay(1);
        }
        SDL_Delay(50);
        return 0;
    }
    // K: Oversize response (17 MiB, no newline) AND immediate
    // child exit. Exercises the shared consume helper from both
    // the regular read phase AND the bounded final-reply drain
    // path: whichever phase catches the buffered bytes past the
    // 16 MiB cap must trip the cap message and drop the data
    // without copying it into a receipt. Ordering between the
    // two paths is OS-dependent; the assertion accepts either
    // terminal signal.
    if (scenario == "oversize_then_exit") {
        char rbuf[256];
        if (std::fgets(rbuf, sizeof(rbuf), stdin) == nullptr)
            return 0;
        std::vector<char> burst(17 * 1024 * 1024, 'Y');
        std::size_t off = 0;
        while (off < burst.size()) {
            const std::size_t chunk = std::min<std::size_t>(64 * 1024, burst.size() - off);
            const std::size_t n = std::fwrite(burst.data() + off, 1, chunk, stdout);
            if (n > 0)
                off += n;
            else
                SDL_Delay(1);
        }
        std::fflush(stdout);
        // Immediate exit — do not stay alive.
        std::exit(0);
    }
    // E3: stderr flood — 200 KiB to stderr, 1 KiB at a time.
    // Must read parent's request first (production one-request
    // contract: response only follows a request).
    if (scenario == "stderr_flood") {
        char rbuf[256];
        if (std::fgets(rbuf, sizeof(rbuf), stdin) == nullptr)
            return 0;
        for (int i = 0; i < 200; ++i) {
            std::string s(1024, 'E');
            std::fwrite(s.data(), 1, s.size(), stderr);
        }
        // A short valid response so main can drain something.
        std::fprintf(stdout, "{\"protocol\":2,\"id\":1,\"ok\":true}\n");
        std::fflush(stdout);
        SDL_Delay(50);
        return 0;
    }
    // E4: Malformed JSON — child writes a non-JSON line.
    // Must read parent's request first.
    if (scenario == "malformed_json") {
        char rbuf[256];
        if (std::fgets(rbuf, sizeof(rbuf), stdin) == nullptr)
            return 0;
        fputs("this is not json\n", stdout);
        std::fflush(stdout);
        SDL_Delay(50);
        return 0;
    }
    // F1: Quit reply + clean exit.
    // Must read parent's request first.
    if (scenario == "quit_then_exit") {
        char rbuf[256];
        if (std::fgets(rbuf, sizeof(rbuf), stdin) == nullptr)
            return 0;
        std::fprintf(stdout, "{\"protocol\":2,\"id\":1,\"ok\":true,\"quit_requested\":true,"
                             "\"diagnostic\":\"goodbye\"}\n");
        std::fflush(stdout);
        return 0;
    }
    // F2: Close stdout while alive.
    if (scenario == "close_stdout_live") {
        // fclose stdout; stay alive.
        fclose(stdout);
        for (;;)
            SDL_Delay(1000);
    }
    // G: idle (one line, then never answer).
    // Must read parent's request first.
    if (scenario == "idle_after_one_line") {
        char rbuf[256];
        if (std::fgets(rbuf, sizeof(rbuf), stdin) == nullptr)
            return 0;
        std::fprintf(stdout, "{\"protocol\":2,\"id\":1,\"ok\":true}\n");
        std::fflush(stdout);
        for (;;)
            SDL_Delay(1000);
    }
    // I: Reply once, then crash (non-zero exit). Used to test
    // "unexpected exit after ordinary reply": the runtime sent a
    // valid receipt and then crashed; the worker must surface a
    // failure (NOT silently drain) so PlaySession can react.
    // Must read parent's request first.
    if (scenario == "reply_then_crash") {
        char rbuf[256];
        if (std::fgets(rbuf, sizeof(rbuf), stdin) == nullptr)
            return 0;
        std::fprintf(stdout, "{\"protocol\":2,\"id\":1,\"ok\":true}\n");
        std::fflush(stdout);
        SDL_Delay(50);
        // Crash with a non-zero exit so the test can assert that
        // PlaySession distinguishes crash from graceful Quit.
        std::exit(137);
    }
    std::cerr << "[child] unknown scenario: " << scenario << std::endl;
    return 2;
}
} // namespace child

int main(int argc, char** argv) {
    {
        const int rc = child::run(argc, argv);
        if (argc >= 3 && std::strcmp(argv[2], "--child") == 0)
            return rc;
    }
    // All scenarios are wrapped in a try/catch so a `require`
    // failure unwinds cleanly via exceptions. The outer try
    // prints the failure and returns non-zero; no child process
    // is left alive because every scenario uses the ChildGuard
    // RAII wrapper below.
    try {
        require(SDL_Init(0) == true, std::string("SDL_Init failed: ") + SDL_GetError());

        // -- Scenario A: Large response, main sleeps > 5 s. -----------
        // Verifies: worker reads 70 KiB in chunks; main's 6 s sleep
        // does NOT cause a timeout because receipt is on-time.
        //
        // Documented framing (from the child):
        //   wire = header(40) + data(N) + footer("}\n" = 3)
        //   where N = 70 * 1024 - header - footer = 71637
        //   total wire bytes = 70 * 1024 = 71680
        //   receipt (newline stripped) = 71680 - 1 = 71679
        // Child stays alive 10 s after writing so the worker does
        // NOT see EOF before the parent drains the receipt.
        {
            log("scenario A: large response (71679 bytes), main sleeps 6 s");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "large_response");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario A: worker start");
            // Ownership transferred to worker. Subsequent require
            // failures are cleaned up by `w`'s destructor; guard does
            // nothing.
            guard.child = nullptr;
            const std::string req = "{\"protocol\":2,\"id\":1,\"command\":\"hi\"}\n";
            const auto sent_at = forge::PlayTransportWorker::monotonic_ms();
            require(w.submit(req, sent_at), "scenario A: submit");
            std::this_thread::sleep_for(std::chrono::milliseconds(6000));
            forge::PlayTransportWorker::Receipt r;
            require(w.drain_one_line(r), "scenario A: no receipt after 6 s");
            // EXACT byte count: 70 KiB wire minus the trailing newline
            // that the worker strips during newline framing.
            require(r.payload.size() == 70 * 1024 - 1,
                    "scenario A: expected 71679 bytes, got " + std::to_string(r.payload.size()));
            // EXACT framing: starts with the documented header and
            // ends with the closing quote + brace.
            const std::string expected_prefix = "{\"protocol\":2,\"id\":1,\"ok\":true,\"data\":\"";
            const std::string expected_suffix = "\"}";
            require(r.payload.compare(0, expected_prefix.size(), expected_prefix) == 0,
                    "scenario A: payload prefix mismatch");
            require(r.payload.compare(r.payload.size() - expected_suffix.size(),
                                      expected_suffix.size(), expected_suffix) == 0,
                    "scenario A: payload suffix mismatch");
            // The body between prefix and suffix is exactly the
            // configured payload size of 'X' bytes.
            const std::size_t body_size =
                r.payload.size() - expected_prefix.size() - expected_suffix.size();
            const std::size_t expected_body =
                70 * 1024 - expected_prefix.size() - expected_suffix.size() - 1;
            require(body_size == expected_body, "scenario A: body size " +
                                                    std::to_string(body_size) + " != expected " +
                                                    std::to_string(expected_body));
            for (std::size_t i = 0; i < body_size; ++i) {
                require(r.payload[expected_prefix.size() + i] == 'X',
                        "scenario A: body byte mismatch at offset " + std::to_string(i));
            }
            require(r.received_at_ms != 0, "scenario A: received_at_ms unset");
            // On-time: receipt must be well under the 5 s deadline.
            require(r.received_at_ms - sent_at < 5000,
                    "scenario A: receipt was late (" + std::to_string(r.received_at_ms - sent_at) +
                        " ms)");
            // No timeout failure: the child stays alive, so EOF
            // failure is NOT expected within the test window.
            // Take ONCE; assert empty directly (no wait_for for
            // absence — the failure slot is a snapshot, not a
            // signal).
            const std::string a_failure = w.take_failure();
            require(a_failure.empty(),
                    "scenario A: worker surfaced an unexpected failure: " + a_failure);
            // `w` destructor (on scope exit) handles stop/join/destroy
            // of the transferred SDL_Process*.
        }

        // -- Scenario B: Silent consumer, deadline fires. --------------
        // Verifies: NOT_READY does not busy spin; deadline fires
        // within the bound; stop+join returns.
        {
            log("scenario B: child never reads stdin, deadline fires");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "silent_consumer");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario B: worker start");
            guard.child = nullptr;
            // Submit a 1 MiB line — pipe fills before all bytes drain.
            const std::string req(1024 * 1024, 'X');
            const auto sent_at = forge::PlayTransportWorker::monotonic_ms();
            require(w.submit(req + "\n", sent_at), "scenario B: submit");
            // Wait up to 6.5 s for the deadline failure. Capture the
            // failure string ONCE in the predicate so the assertion
            // below reads the actual cause, not an empty slot.
            std::string failure;
            require(wait_for(
                        [&] {
                            failure = w.take_failure();
                            return !failure.empty();
                        },
                        6500),
                    "scenario B: no failure surfaced within deadline");
            require(failure.find("timed out") != std::string::npos,
                    "scenario B: expected timeout failure, got: " + failure);
            // stop+join returns within a short grace.
            const auto t0 = std::chrono::steady_clock::now();
            w.stop();
            w.join();
            const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
            require(dt < 500, "scenario B: stop+join hung for " + std::to_string(dt) + " ms");
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario C: Reads request, never answers. ---------------
        // Verifies: after write completes, AwaitingResponse deadline
        // still fires within 5 s without main pumping.
        {
            log("scenario C: read request, never answer; deadline fires");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "read_no_answer");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario C: worker start");
            guard.child = nullptr;
            const std::string req = "{\"protocol\":2,\"id\":1,\"command\":\"hi\"}\n";
            require(w.submit(req, forge::PlayTransportWorker::monotonic_ms()),
                    "scenario C: submit");
            // Do NOT pump or wait — just wait for the deadline (5 s)
            // without any main-thread action. Capture failure once.
            std::string failure;
            require(wait_for(
                        [&] {
                            failure = w.take_failure();
                            return !failure.empty();
                        },
                        6500),
                    "scenario C: no failure surfaced (write never completed?)");
            require(failure.find("timed out") != std::string::npos,
                    "scenario C: expected timeout failure, got: " + failure);
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario D: late receipt via worker-direct. -------------
        // Verifies: worker rejects a receipt whose received_at_ms is
        // past the deadline (does NOT publish). The production
        // PlaySession path is exercised by
        // tests/sdk_play_editor_transport_tests.cpp (manager ctest
        // `native_sdk_editor_transport`); launching the test binary
        // as a PlaySession runtime here is unsafe — recursive self-
        // launch would re-execute the entire suite.
        {
            log("scenario D: worker rejects late receipt");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "late_answer");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario D: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario D: submit");
            // Worker is the authoritative deadline owner: at T=5 s
            // the deadline check fires before the late reply can
            // arrive. The "first failure wins" rule (set_failure
            // only writes if slot is empty) means the failure
            // string is the timeout message, not a late-receipt or
            // EOF message.
            std::string failure;
            require(wait_for(
                        [&] {
                            failure = w.take_failure();
                            return !failure.empty();
                        },
                        7000),
                    "scenario D: no failure within 7 s for late reply");
            require(failure.find("timed out") != std::string::npos,
                    "scenario D: expected timeout failure, got: " + failure);
            // The receipt must NOT have been published (worker is
            // authoritative on late-receipt rejection).
            forge::PlayTransportWorker::Receipt r;
            require(!w.drain_one_line(r), "scenario D: worker published a late receipt");
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario E1: partial then complete newline. -------------
        {
            log("scenario E1: partial newline framing");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "partial_then_complete");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario E1: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario E1: submit");
            require(wait_for(
                        [&] {
                            forge::PlayTransportWorker::Receipt r;
                            return w.drain_one_line(r);
                        },
                        2000),
                    "scenario E1: no receipt");
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario E2: oversize no newline. ----------------------
        // ponytail: the harness wait_for deadline is 30 s, not 5 s,
        // because instrumentation (TClang TSAN) adds ~5-10x overhead
        // per SDL_ReadIO and per instrumented memchr on a growing
        // 16 MiB buffer; the real 5000 ms receive deadline in the
        // production worker is unchanged. This is a TEST harness
        // deadline, not a production timeout. B/C/D use the
        // production 5000 ms receive deadline unchanged.
        {
            log("scenario E2: oversize inbound (17 MiB, no newline)");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "oversize_no_newline");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario E2: worker start");
            guard.child = nullptr;
            std::string failure;
            const bool e2_signaled = wait_for(
                [&] {
                    failure = w.take_failure();
                    return !failure.empty();
                },
                30000);
            if (!e2_signaled) {
                std::string stderr_dump;
                w.drain_stderr(stderr_dump);
                std::cerr << "[fail] scenario E2 timeout evidence: stderr_tail_size="
                          << stderr_dump.size()
                          << " worker_running=" << (w.running() ? "true" : "false") << std::endl;
            }
            require(e2_signaled, "scenario E2: no failure surfaced on overrun");
            require(failure.find("exceeded cap") != std::string::npos,
                    "scenario E2: expected overflow failure, got: " + failure);
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario E3: stderr flood. -----------------------------
        {
            log("scenario E3: stderr flood (200 KiB)");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "stderr_flood");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario E3: worker start");
            guard.child = nullptr;
            // Submit a request first so the child (which reads
            // before responding) is in a valid request/response
            // flow. The stderr flood + the response are both
            // side-effects of this single request.
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario E3: submit");
            forge::PlayTransportWorker::Receipt r;
            require(wait_for([&] { return w.drain_one_line(r); }, 3000),
                    "scenario E3: no receipt from child");
            require(r.payload.find("\"ok\":true") != std::string::npos,
                    "scenario E3: receipt payload mismatch, got: " + r.payload);
            std::string stderr_dump;
            w.drain_stderr(stderr_dump);
            require(stderr_dump.size() <= 64 * 1024,
                    "scenario E3: stderr tail exceeded 64 KiB cap (" +
                        std::to_string(stderr_dump.size()) + ")");
            require(stderr_dump.size() > 0, "scenario E3: stderr drain returned empty");
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario E4: worker delivers opaque bytes intact. -------
        // Verifies the transport delivers raw bytes verbatim
        // regardless of payload validity. The worker is byte-only;
        // it does NOT parse JSON. Main-side JSON parse failure
        // (PlaySession::pump applying the receipt) is covered by
        // a separate path and is not exercised here.
        //
        // Existing PlaySession malformed-protocol coverage:
        // tests/sdk_play_editor_transport_tests.cpp and
        // tests/editor_sdk_tests.cpp do NOT currently assert on
        // malformed JSON; that coverage is owned by PlaySession
        // tests, not the transport-worker harness.
        {
            log("scenario E4: worker delivers opaque bytes intact");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "malformed_json");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario E4: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario E4: submit");
            // Capture receipt unconditionally (single drain). No
            // optional-assert that silently skips.
            forge::PlayTransportWorker::Receipt r;
            require(wait_for([&] { return w.drain_one_line(r); }, 2000), "scenario E4: no receipt");
            require(r.payload == "this is not json",
                    "scenario E4: payload mismatch, got: " + r.payload);
            // No further drain — receipt slot is now empty. `w`'s
            // destructor handles stop/join/destroy on scope exit.
        }

        // -- Scenario F1: Quit reply + clean exit. ------------------
        // Verifies the worker's bounded final-reply drain: when the
        // runtime sends a complete newline reply and then exits, the
        // worker publishes the receipt via drain_and_publish_final
        // BEFORE the process-exit failure path runs. The receipt is
        // available to main. The production PlaySession Quit-dispatch
        // path (which calls close_process and avoids overwriting the
        // Quit status) is exercised by
        // tests/sdk_play_editor_transport_tests.cpp (manager ctest
        // `native_sdk_editor_transport`); launching the test binary
        // as a PlaySession runtime here would re-execute the entire
        // suite recursively.
        {
            log("scenario F1: Quit reply + clean exit (worker publishes before failure)");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "quit_then_exit");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario F1: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"quit\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario F1: submit");
            // Allow up to 3 s for the worker to drain the receipt
            // via the bounded final-reply path.
            forge::PlayTransportWorker::Receipt r;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                if (w.drain_one_line(r))
                    break;
                SDL_Delay(10);
            }
            require(!r.payload.empty(), "scenario F1: worker did not publish final Quit receipt");
            require(r.payload.find("quit_requested") != std::string::npos,
                    "scenario F1: receipt missing quit_requested, got: " + r.payload);
            // The process-exit failure string is informational in
            // this scenario (the runtime exited cleanly after sending
            // the Quit reply). Main drains it.
            (void)w.take_failure();
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario F2: close stdout while child alive. -----------
        {
            log("scenario F2: child closes stdout while alive");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "close_stdout_live");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario F2: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario F2: submit");
            // Capture the failure string ONCE into a local — the
            // take_failure() predicate must not re-read the (now
            // empty) failure slot.
            std::string failure;
            require(wait_for(
                        [&] {
                            failure = w.take_failure();
                            return !failure.empty();
                        },
                        2000),
                    "scenario F2: no failure on EOF-while-alive");
            require(failure.find("closed") != std::string::npos ||
                        failure.find("EOF") != std::string::npos,
                    "scenario F2: expected stdout closed failure, got: " + failure);
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario G: stop while IO pending; restart SAME worker. -
        {
            log("scenario G: stop / restart SAME worker instance");
            forge::PlayTransportWorker w;
            // Each spawned child gets its own guard scoped to one
            // iteration of the cycle. After `w.start` succeeds the
            // guard is released; `w`'s destructor handles cleanup.
            for (int cycle = 0; cycle < 3; ++cycle) {
                ChildGuard guard;
                auto c = spawn_child(argv[0], "silent_consumer");
                guard.child = c.process;
                require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                        "scenario G cycle " + std::to_string(cycle) + ": start");
                guard.child = nullptr;
                require(w.submit(std::string(1024 * 1024, 'X') + "\n",
                                 forge::PlayTransportWorker::monotonic_ms()),
                        "scenario G cycle " + std::to_string(cycle) + ": submit");
                // Stop immediately while IO is pending (worker blocked
                // on NOT_READY). Explicit stop/join between cycles
                // because `w` outlives the loop iteration.
                const auto t0 = std::chrono::steady_clock::now();
                w.stop();
                w.join();
                const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
                require(dt < 1000, "scenario G cycle " + std::to_string(cycle) +
                                       ": stop+join took " + std::to_string(dt) + " ms");
                require(!w.started(), "scenario G cycle " + std::to_string(cycle) +
                                          ": started() true after join");
                // Cycle's guard destructor does nothing (child == nullptr).
            }
            // Submit before any start: must fail.
            require(!w.submit("{\"x\":1}\n", forge::PlayTransportWorker::monotonic_ms()),
                    "scenario G: submit before start accepted");
            // Restart with idle_after_one_line child.
            {
                ChildGuard guard;
                auto c = spawn_child(argv[0], "idle_after_one_line");
                guard.child = c.process;
                require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                        "scenario G restart: start");
                guard.child = nullptr;
                require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                                 forge::PlayTransportWorker::monotonic_ms()),
                        "scenario G restart: submit");
                require(wait_for(
                            [&] {
                                forge::PlayTransportWorker::Receipt r;
                                return w.drain_one_line(r);
                            },
                            2000),
                        "scenario G restart: no receipt from new session");
                // Submit another after restart; worker must NOT have any
                // leftover receipt/error blocking it.
                require(w.submit("{\"protocol\":2,\"id\":2,\"command\":\"y\"}\n",
                                 forge::PlayTransportWorker::monotonic_ms()),
                        "scenario G restart: second submit");
                // Inner guard destructor does nothing on success.
            }
            // `w` destructor handles final cleanup at scenario exit.
        }

        // -- Scenario H: existing sdk_play_editor_transport_tests ---
        // These tests live in tests/sdk_play_editor_transport_tests.cpp
        // and tests/editor_sdk_tests.cpp. They exercise the
        // correlation / stale / protocol semantics of PlaySession and
        // are NOT modified by this work. This scenario does not fake-
        // assert anything about them; the runnable gate is the
        // manager's ctest invocation below.
        //
        // Required CTest commands (manager-only; this binary does
        // not execute them):
        //
        //   cmake --build /opt/forge-build --target forge_play_transport_worker_tests
        //   cmake --build /opt/forge-build --target forge_sdk_editor_tests
        //   cmake --build /opt/forge-build --target forge_sdk_editor_transport_tests
        //   ctest --test-dir /opt/forge-build -R play_transport_worker     --output-on-failure
        //   --timeout 60 ctest --test-dir /opt/forge-build -R native_sdk_editor_transport
        //   --output-on-failure --timeout 45 ctest --test-dir /opt/forge-build -R
        //   native_sdk_editor_play    --output-on-failure --timeout 45
        //
        // forge_sdk_editor_tests and forge_sdk_editor_transport_tests
        // require forge_authoring + forge_authored_inspection +
        // forge_game_platform (linked in CMakeLists.txt). On Linux
        // forge_editor itself is not built (Windows-only).
        {
            log("scenario H: existing PlaySession transport tests (unchanged; manager-owned ctest "
                "gate)");
        }

        // -- Scenario I: unexpected exit after ordinary reply. --------
        // Runtime sends a valid receipt, then crashes (exit 137).
        // The worker must surface a failure (the receipt IS
        // published via drain_and_publish_final, but the process-
        // exit failure also lands in failure_). Main drains the
        // receipt FIRST, then observes the failure. With the new
        // main-side policy this is a real session fault.
        {
            log("scenario I: unexpected exit after ordinary reply");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "reply_then_crash");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario I: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario I: submit");
            // Receipt publication and process-exit detection are
            // distinct events. Wait boundedly for BOTH. Capture
            // outside the loop so take_failure is consumed at most
            // once across iterations — failure slot is a one-shot
            // snapshot, not a signal. Break only when BOTH ready.
            forge::PlayTransportWorker::Receipt r;
            bool got_receipt = false;
            std::string failure;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                if (!got_receipt)
                    got_receipt = w.drain_one_line(r);
                if (failure.empty())
                    failure = w.take_failure();
                if (got_receipt && !failure.empty())
                    break;
                SDL_Delay(10);
            }
            // Receipt must be published via bounded final-reply
            // drain (worker reads the reply before the child exits).
            require(got_receipt, "scenario I: worker did not publish pre-exit receipt");
            require(r.payload.find("\"ok\":true") != std::string::npos,
                    "scenario I: receipt payload mismatch, got: " + r.payload);
            // Failure must be surfaced. OS event ordering is not
            // pinned: stdout EOF can be observed before SDL_WaitProcess
            // returns true (or vice versa). Accept either terminal
            // signal as long as the receipt was published and a
            // post-receipt process-end state is reported. The
            // dedicated stdout-closed-while-alive case is F2.
            require(!failure.empty(), "scenario I: no failure surfaced after child reply+exit");
            require(failure.find("exited") != std::string::npos ||
                        failure.find("stdout closed") != std::string::npos ||
                        failure.find("EOF") != std::string::npos,
                    "scenario I: expected runtime-exited or stdout-closed EOF, got: " + failure);
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario J: mailbox_busy_ gate. ------------------------
        // A second submit() is rejected while the first request is
        // queued / writing / awaiting / receipt-ready. Allowed only
        // after drain_one_line returns the receipt.
        {
            log("scenario J: mailbox_busy_ rejects second submit");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "idle_after_one_line");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario J: worker start");
            guard.child = nullptr;
            const auto now = forge::PlayTransportWorker::monotonic_ms();
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n", now),
                    "scenario J: first submit");
            // Second submit MUST be rejected (mailbox_busy_).
            require(!w.submit("{\"protocol\":2,\"id\":2,\"command\":\"y\"}\n", now),
                    "scenario J: second submit accepted while busy");
            // Empty submit rejected.
            require(!w.submit("", now), "scenario J: empty submit accepted");
            // Oversize submit rejected (17 MiB line).
            require(!w.submit(std::string(17 * 1024 * 1024, 'Z') + "\n", now),
                    "scenario J: oversize submit accepted");
            // Drain the receipt.
            forge::PlayTransportWorker::Receipt r;
            require(wait_for([&] { return w.drain_one_line(r); }, 2000), "scenario J: no receipt");
            // After drain, a follow-on submit is accepted.
            require(w.submit("{\"protocol\":2,\"id\":3,\"command\":\"z\"}\n", now),
                    "scenario J: submit after drain rejected");
            // `w` destructor handles stop/join/destroy.
        }

        // -- Scenario K: oversize stream rejection with child exit. -
        // Child writes 17 MiB with no newline (past the 16 MiB cap)
        // and exits. The shared consume helper enforces the inbound
        // cap on every chunk append BEFORE any framing or receipt
        // move, so an oversize burst must trip the documented
        // "exceeded cap" failure and drop the data without copying
        // it into a receipt. The exact path that observes the cap
        // (regular read phase vs bounded final-reply drain) is
        // OS-dependent; this scenario asserts the OUTCOME (cap
        // failure surfaced, no receipt published), not which
        // internal branch delivered it. Companion to E2 (which
        // keeps the child alive and exercises the same cap).
        //
        // ponytail: harness deadline 30 s for instrumentation
        // headroom (TClang TSAN SDL_ReadIO intercept); production
        // 5000 ms receive deadline is unchanged. B/C/D runtime
        // deadlines unchanged.
        {
            log("scenario K: oversize stream rejection with child exit");
            ChildGuard guard;
            auto c = spawn_child(argv[0], "oversize_then_exit");
            guard.child = c.process;
            forge::PlayTransportWorker w;
            require(w.start(c.process, c.stdin_pipe, c.stdout_pipe, c.stderr_pipe),
                    "scenario K: worker start");
            guard.child = nullptr;
            require(w.submit("{\"protocol\":2,\"id\":1,\"command\":\"x\"}\n",
                             forge::PlayTransportWorker::monotonic_ms()),
                    "scenario K: submit");
            // Wait boundedly for the cap failure. This scenario
            // does NOT accept arbitrary terminal errors as cap
            // proof: only the documented "exceeded cap" message
            // qualifies.
            std::string failure;
            const bool k_signaled = wait_for(
                [&] {
                    failure = w.take_failure();
                    return !failure.empty();
                },
                30000);
            if (!k_signaled) {
                std::string stderr_dump;
                w.drain_stderr(stderr_dump);
                std::cerr << "[fail] scenario K timeout evidence: stderr_tail_size="
                          << stderr_dump.size()
                          << " worker_running=" << (w.running() ? "true" : "false") << std::endl;
            }
            require(k_signaled, "scenario K: no failure surfaced on oversize stream");
            require(failure.find("exceeded cap") != std::string::npos,
                    "scenario K: expected 'exceeded cap' failure, got: " + failure);
            // No receipt published: the cap path drops the data
            // before any framing move. drain_one_line must return
            // false.
            forge::PlayTransportWorker::Receipt r;
            require(!w.drain_one_line(r), "scenario K: worker published an oversize receipt");
            // `w` destructor handles stop/join/destroy.
        }

        SDL_Quit();
        log("all scenarios completed");
        return 0;
    } catch (const TestFailure& e) {
        std::cerr << "[fail] caught: " << e.what() << std::endl;
        SDL_Quit();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "[fail] unexpected exception: " << e.what() << std::endl;
        SDL_Quit();
        return 3;
    }
}