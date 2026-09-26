#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace forge {
// One byte-transport owner for a single PlaySession runtime subprocess.
//
// Ownership contract (pinned SDL3.4.16):
//   * SDL_GetProcessInput/Output/Properties — safe from any thread;
//     main thread grabs the SDL_IOStream* pointers ONCE during
//     launch() and hands them to the worker via start().
//   * SDL_ReadIO / SDL_WriteIO / SDL_GetIOStatus — exclusive to
//     the worker thread (SDL doc "Do not use the same SDL_IOStream
//     from two threads at once").
//   * SDL_WaitProcess / SDL_KillProcess / SDL_DestroyProcess /
//     SDL_ReadProcess — exclusive to the worker thread
//     (SDL doc "not thread safe").
//
// Single mailbox mutex + condition variable. The worker moves the
// queued request under a short lock into local active state,
// releases the lock, then performs IO. The worker publishes a
// completed receipt under the same lock. Main drains the receipt
// under the same lock. IO / process waits / JSON parse / callbacks
// / graphics all stay OUTSIDE the mutex.
//
// State machine:
//   Idle --take--> Writing --all bytes written--> AwaitingResponse
//                                                 |
//              +------+------+--------+------------+------------+
//              v             v         v                          v
//         ReceiptReady   Failed   EOF/ERROR/exit         (no in-flight receipt)
//
//   ReceiptReady --main drain--> Idle
//   Failed/Stopping --shutdown--> Stopped
//
// Deadline: activated at submit (mailbox_deadline_ms_ =
// sent_at_ms + 5000). Persists across Writing -> AwaitingResponse.
// Cleared only at AwaitingResponse -> ReceiptReady (on-time) or
// Failed. Already-received valid bytes remain available; UI stall
// after receipt cannot retroactively timeout.
//
// Single-receipt protocol: while has_receipt_ is true the worker
// refuses a second receipt (protocol violation -> Failed).
class PlayTransportWorker {
  public:
    // Outbound request byte cap. Matches the prior 16 MiB ceiling.
    static constexpr std::size_t kOutboundMaxBytes = 16 * 1024 * 1024;
    // Inbound raw partial-line buffer cap. Overrun: drop data,
    // surface failure, do NOT push a giant copy.
    static constexpr std::size_t kInboundTotalBytesMax = 16 * 1024 * 1024;
    // Stderr tail cap.
    static constexpr std::size_t kStderrTailBytesMax = 65536;
    // Read chunk size.
    static constexpr std::size_t kReadChunkBytes = 8192;
    // Bounded IO passes (keep deadlock + starvation impossible).
    static constexpr int kReadIterationsPerPass = 32;
    static constexpr int kStderrIterationsPerPass = 8;
    // Idle wait timeout. Long enough to avoid busy spin; short
    // enough to keep deadline detection responsive.
    static constexpr auto kIdleWait = std::chrono::milliseconds(2);
    // Receive deadline ceiling (matches the prior 5 s timeout).
    // Boundary is STRICTLY GREATER: a receipt timestamped exactly
    // at `sent_at_ms + 5000` is still on time. Both the worker
    // timeout check and the late-receipt check use `>` (NOT `>=`).
    // Documented in the run_loop deadline block.
    static constexpr std::uint64_t kDeadlineMs = 5000;

    struct Receipt {
        std::string payload;
        std::uint64_t received_at_ms = 0;
    };

    PlayTransportWorker() = default;
    ~PlayTransportWorker();
    PlayTransportWorker(const PlayTransportWorker&) = delete;
    PlayTransportWorker& operator=(const PlayTransportWorker&) = delete;

    // Bind a freshly-created process + its IOStream pointers to a
    // new worker thread. Validates handles. Resets all per-start
    // state (mailbox, receipt, failure, stderr tail, started
    // flag). On std::thread construction failure, resets local
    // state so the caller's "if start() returns false the handles
    // are NOT consumed" contract holds.
    bool start(SDL_Process* process, SDL_IOStream* stdin_stream, SDL_IOStream* stdout_stream,
               SDL_IOStream* stderr_stream);

    // Enqueue a wire request. Returns false if the worker is not
    // started, has been asked to stop, the mailbox already has a
    // pending request, or the request exceeds kOutboundMaxBytes
    // (size checked BEFORE storing). Worker stores `sent_at_ms`
    // and computes `mailbox_deadline_ms_ = sent_at_ms +
    // kDeadlineMs`. Notifies the cv so the worker wakes on new
    // work even if blocked on idle wait.
    bool submit(std::string request_line, std::uint64_t sent_at_ms);

    // Drain a single completed receipt. Returns true and fills
    // `out` when a receipt was available; returns false when the
    // mailbox has no receipt. NOT idempotent across concurrent
    // callers — single-caller contract.
    bool drain_one_line(Receipt& out);

    // Drain newly-appended stderr bytes into `out`. Returns the
    // count of bytes appended.
    std::size_t drain_stderr(std::string& out);

    // Take the most-recent failure string set by the worker (or
    // empty if none). One-shot.
    std::string take_failure();

    // Worker-thread lifetime observation.
    bool started() const { return started_.load(std::memory_order_acquire); }
    bool running() const { return running_.load(std::memory_order_acquire); }

    // Single-owner shutdown. Idempotent.
    void stop();

    // Wait for the worker thread to finish. Idempotent.
    void join();

    // Monotonic millisecond timestamp from the single owner clock
    // (std::chrono::steady_clock). Used by both threads so the
    // sent_at_ms and received_at_ms values are directly comparable.
    static std::uint64_t monotonic_ms();

  private:
    void run_loop();
    void shutdown_and_destroy();

    // Worker-thread-owned after start() succeeds. Cleared by
    // shutdown_and_destroy().
    SDL_Process* process_ = nullptr;
    SDL_IOStream* stdin_ = nullptr;
    SDL_IOStream* stdout_ = nullptr;
    SDL_IOStream* stderr_stream_ = nullptr;

    // Shared mailbox (one mutex + cv). Every read/write of these
    // fields holds mailbox_mutex_.
    std::mutex mailbox_mutex_;
    std::condition_variable mailbox_cv_;
    // mailbox_busy_ is the OUTSTANDING-REQUEST gate. True from
    // successful submit() until the receipt is drained by main, OR
    // the worker enters Failed, OR stop() flips stop_requested_. It
    // covers every state where another submit() must be rejected:
    // queued-in-mailbox, writing, awaiting response, and receipt
    // ready (not yet drained by main). Cleared under the same
    // mailbox_mutex_ that submit() reads.
    bool mailbox_busy_ = false;
    bool mailbox_has_request_ = false;
    std::string mailbox_line_;
    std::uint64_t mailbox_sent_at_ms_ = 0;
    std::uint64_t mailbox_deadline_ms_ = 0;
    bool has_receipt_ = false;
    std::string receipt_payload_;
    std::uint64_t receipt_received_at_ms_ = 0;

    // Worker-LOCAL (only run_loop touches).
    bool in_flight_ = false;
    std::string pending_line_;
    std::size_t pending_offset_ = 0;
    std::uint64_t pending_sent_at_ms_ = 0;
    std::uint64_t pending_deadline_ms_ = 0;
    std::string incoming_buffer_;

    // Failure string + stderr tail (separate mutexes so failure
    // teardown is independent of stderr capture).
    std::mutex failure_mutex_;
    std::string failure_;

    std::mutex stderr_mutex_;
    std::string stderr_tail_;

    std::atomic<bool> started_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace forge