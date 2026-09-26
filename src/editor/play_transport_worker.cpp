#include "play_transport_worker.hpp"
#include <exception>
#include <utility>

namespace forge {
std::uint64_t PlayTransportWorker::monotonic_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

PlayTransportWorker::~PlayTransportWorker() {
    stop();
    join();
}

namespace {
// RAII guard: ALWAYS kills / waits / destroys the SDL_Process*
// when control leaves the scope, even if an exception unwinds
// through the surrounding diagnostic work. SDL_DestroyProcess is
// not thread safe per pinned SDL3.4.16 src/process/SDL_process.c
// and this is the single owner.
struct ProcessHandleGuard {
    SDL_Process*& slot;
    ~ProcessHandleGuard() noexcept {
        if (slot) {
            // SDL_KillProcess / SDL_WaitProcess / SDL_DestroyProcess
            // are C functions and do not throw. The slot is cleared
            // even on partial failure so the destructor cannot run
            // twice.
            SDL_KillProcess(slot, true);
            SDL_WaitProcess(slot, true, nullptr);
            SDL_DestroyProcess(slot);
            slot = nullptr;
        }
    }
};
} // namespace

void PlayTransportWorker::shutdown_and_destroy() {
    // Single-call teardown. Caller guarantees this runs only on
    // the worker thread after the run_loop exit path. All SDL
    // calls here are NOT thread safe per doc and this is their
    // single owner.
    //
    // Order: install the RAII guard FIRST so the process handle is
    // killed+destroyed even if any of the best-effort diagnostic
    // steps below throw. Any complete-line receipt the run_loop
    // published BEFORE this point is already in the mailbox;
    // partial bytes in incoming_buffer_ that never framed into a
    // newline are reported in the failure string (no further
    // attempt to publish a partial line — the protocol requires
    // newline framing).
    ProcessHandleGuard guard{process_};
    // Stderr drain: bounded tail-append. Use a fixed local buffer
    // so this block does not allocate; lock contention cannot
    // throw here.
    if (stderr_stream_) {
        char buf[kReadChunkBytes];
        std::string local;
        for (int i = 0; i < 8; ++i) {
            const auto n = SDL_ReadIO(stderr_stream_, buf, sizeof(buf));
            if (n == 0)
                break;
            local.append(buf, n);
        }
        if (!local.empty()) {
            std::lock_guard<std::mutex> lk(stderr_mutex_);
            stderr_tail_.append(local);
            if (stderr_tail_.size() > kStderrTailBytesMax)
                stderr_tail_.erase(0, stderr_tail_.size() - kStderrTailBytesMax);
        }
    }
    stdin_ = nullptr;
    stdout_ = nullptr;
    stderr_stream_ = nullptr;
}

namespace {
void set_failure(std::mutex& m, std::string& slot, std::string msg) {
    std::lock_guard<std::mutex> lk(m);
    if (slot.empty())
        slot = std::move(msg);
}
} // namespace

bool PlayTransportWorker::start(SDL_Process* process, SDL_IOStream* stdin_stream,
                                SDL_IOStream* stdout_stream, SDL_IOStream* stderr_stream) {
    if (!process || !stdin_stream || !stdout_stream || !stderr_stream)
        return false;
    if (thread_.joinable())
        return false;
    // Reset ALL per-start state so a restart on the same worker
    // exposes no stale bytes, receipts, errors, or pending writes.
    {
        std::lock_guard<std::mutex> lk(mailbox_mutex_);
        mailbox_busy_ = false;
        mailbox_has_request_ = false;
        mailbox_line_.clear();
        mailbox_sent_at_ms_ = 0;
        mailbox_deadline_ms_ = 0;
        has_receipt_ = false;
        receipt_payload_.clear();
        receipt_received_at_ms_ = 0;
    }
    {
        std::lock_guard<std::mutex> lk(failure_mutex_);
        failure_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(stderr_mutex_);
        stderr_tail_.clear();
    }
    in_flight_ = false;
    pending_line_.clear();
    pending_offset_ = 0;
    pending_sent_at_ms_ = 0;
    pending_deadline_ms_ = 0;
    incoming_buffer_.clear();
    stop_requested_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    started_.store(false, std::memory_order_release);
    process_ = process;
    stdin_ = stdin_stream;
    stdout_ = stdout_stream;
    stderr_stream_ = stderr_stream;
    // std::thread construction can throw on resource exhaustion.
    // Wrap; on failure, reset local state so the caller's "if
    // start() returns false the handles are NOT consumed" holds.
    try {
        thread_ = std::thread([this] { run_loop(); });
    } catch (...) {
        process_ = nullptr;
        stdin_ = nullptr;
        stdout_ = nullptr;
        stderr_stream_ = nullptr;
        return false;
    }
    // Publish `started` AFTER the thread is launched so a racing
    // submit() cannot enqueue into a worker that has not yet
    // entered its loop.
    started_.store(true, std::memory_order_release);
    return true;
}

bool PlayTransportWorker::submit(std::string request_line, std::uint64_t sent_at_ms) {
    // Input validation FIRST. No moved-from size arithmetic; check
    // the caller's line BEFORE storing.
    if (request_line.empty())
        return false;
    if (request_line.size() > kOutboundMaxBytes)
        return false;
    // Lifecycle gate: must be started, must not be stopping.
    if (!started_.load(std::memory_order_acquire))
        return false;
    if (stop_requested_.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lk(mailbox_mutex_);
    // Recheck stop + busy under the same mutex.
    if (stop_requested_.load(std::memory_order_acquire))
        return false;
    // mailbox_busy_ covers queued + writing + awaiting + receipt-
    // ready. A second submit is rejected until the receipt is
    // drained OR the worker enters Failed.
    if (mailbox_busy_)
        return false;
    mailbox_line_ = std::move(request_line);
    mailbox_sent_at_ms_ = sent_at_ms;
    mailbox_deadline_ms_ = sent_at_ms + kDeadlineMs;
    mailbox_has_request_ = true;
    mailbox_busy_ = true;
    mailbox_cv_.notify_all();
    return true;
}

bool PlayTransportWorker::drain_one_line(Receipt& out) {
    std::lock_guard<std::mutex> lk(mailbox_mutex_);
    if (!has_receipt_)
        return false;
    out.payload = std::move(receipt_payload_);
    out.received_at_ms = receipt_received_at_ms_;
    has_receipt_ = false;
    receipt_payload_.clear();
    receipt_received_at_ms_ = 0;
    // Receipt consumed — the slot is free for the next submit().
    mailbox_busy_ = false;
    mailbox_cv_.notify_all(); // so a follow-on submit can wake the worker
    return true;
}

std::size_t PlayTransportWorker::drain_stderr(std::string& out) {
    std::lock_guard<std::mutex> lk(stderr_mutex_);
    if (stderr_tail_.empty())
        return 0;
    out.append(stderr_tail_);
    stderr_tail_.clear();
    return out.size();
}

std::string PlayTransportWorker::take_failure() {
    std::lock_guard<std::mutex> lk(failure_mutex_);
    auto out = std::move(failure_);
    failure_.clear();
    return out;
}

void PlayTransportWorker::stop() {
    if (stop_requested_.exchange(true))
        return;
    mailbox_cv_.notify_all();
}

void PlayTransportWorker::join() {
    if (thread_.joinable())
        thread_.join();
    started_.store(false, std::memory_order_release);
    process_ = nullptr;
    stdin_ = nullptr;
    stdout_ = nullptr;
    stderr_stream_ = nullptr;
}

void PlayTransportWorker::run_loop() {
    running_.store(true, std::memory_order_release);
    char read_chunk[kReadChunkBytes];
    bool exit_seen = false;
    std::string exit_diagnostic;
    // Failure-with-clear helper. Records the failure string,
    // clears mailbox_busy_ (so a future submit() is not blocked
    // forever by a failed session), and requests shutdown. Worker-
    // LOCAL `in_flight_` is set false so the take-from-mailbox
    // gate on the next loop iteration does not see a stale true.
    auto fail_now = [this](std::string msg) {
        set_failure(failure_mutex_, failure_, std::move(msg));
        {
            std::lock_guard<std::mutex> lk(mailbox_mutex_);
            mailbox_busy_ = false;
        }
        stop_requested_.store(true, std::memory_order_release);
    };
    // Shared worker-local helper. Both the regular read phase
    // (section 4) and the bounded final-reply drain call this
    // with their chunk. Appends into incoming_buffer_, enforces
    // the inbound cap BEFORE any framing / receipt move (so an
    // oversize read cannot push a giant copy into
    // receipt_payload_), and consumes every complete newline
    // through the publish-or-violate gate.
    //
    // Cap accounting: incoming_buffer_ includes the trailing
    // newline byte, so a 16 MiB buffered partial line + 1 byte
    // newline still trips the cap. The framing byte is part of
    // the wire envelope; budgeting for it is consistent with the
    // doc contract that the line (header + payload + newline) is
    // delivered as one record.
    //
    // Deadline: STRICTLY GREATER THAN pending_deadline_ms_. A
    // receipt read_ts == deadline is on time. Same boundary as
    // the timeout check (section 3) and the late-receipt check
    // (this helper).
    //
    // Returns:
    //   0 = cap exceeded — incoming_buffer_ has been cleared;
    //       caller MUST fail_now and break the loop.
    //   1 = protocol violation during framing — caller MUST
    //       fail_now and break the loop.
    //   2 = clean pass — caller may continue reading.
    // ponytail: O(N^2) framing if a long line accumulates
    // without a newline — each chunk's find('\n') rescans the
    // whole buffer. Minimal cursor fix: scan only the newly
    // appended bytes per call. Evidenced necessary by E2/K
    // timing under TSan instrumentation (17 MiB oversize stream
    // with no newline). Ceiling: O(buffer_size) total scanned
    // across the whole receive, not O(chunks * buffer_size).
    auto consume = [this, &fail_now](const char* data, std::size_t n,
                                     std::uint64_t read_ts) -> int {
        const auto old_size = incoming_buffer_.size();
        incoming_buffer_.append(data, n);
        if (incoming_buffer_.size() > kInboundTotalBytesMax) {
            incoming_buffer_.clear();
            fail_now("Worker inbound buffer exceeded cap (16 MiB)");
            return 0;
        }
        std::size_t scan_from = old_size;
        std::size_t pos;
        while ((pos = incoming_buffer_.find('\n', scan_from)) != std::string::npos) {
            std::string payload;
            payload.assign(incoming_buffer_, 0, pos);
            incoming_buffer_.erase(0, pos + 1);
            // After erase the prefix is gone, so the next
            // find must scan from the start of the now-shorter
            // buffer.
            scan_from = 0;
            bool protocol_violation = false;
            {
                std::lock_guard<std::mutex> lk(mailbox_mutex_);
                if (!in_flight_) {
                    // No pending request — runtime sent an
                    // unexpected response (e.g. during the
                    // post-publish window).
                    protocol_violation = true;
                } else if (read_ts > pending_deadline_ms_) {
                    // Late receipt — do NOT publish.
                    protocol_violation = true;
                } else if (has_receipt_) {
                    // Main slow to drain; refuse to overwrite.
                    protocol_violation = true;
                } else {
                    // On-time receipt -> ReceiptReady. mailbox_busy_
                    // stays true until main drains; that is the
                    // gate a second submit() respects.
                    receipt_payload_ = std::move(payload);
                    receipt_received_at_ms_ = read_ts;
                    has_receipt_ = true;
                    in_flight_ = false;
                    pending_line_.clear();
                    pending_offset_ = 0;
                    mailbox_cv_.notify_all();
                }
            }
            if (protocol_violation) {
                fail_now("Worker received late / unexpected / queued receipt");
                return 1;
            }
        }
        return 2;
    };

    // Bounded final-reply drain. When the worker is about to
    // enter Failed because the runtime process exited, but a
    // request is still in flight, attempt one bounded read pass
    // to consume any remaining stdout and publish a final
    // complete newline through the same `consume` helper above.
    // The run_loop's 32-read-per-pass budget caps the drain; we
    // do NOT block on EOF. After this returns, the caller
    // proceeds to the existing fail_now(exit_diagnostic) path.
    auto drain_and_publish_final = [this, &consume]() {
        if (!stdout_ || !in_flight_)
            return;
        char buf[kReadChunkBytes];
        for (int i = 0; i < kReadIterationsPerPass; ++i) {
            const auto n = SDL_ReadIO(stdout_, buf, sizeof(buf));
            if (n <= 0)
                break;
            const auto rc = consume(buf, static_cast<std::size_t>(n), monotonic_ms());
            if (rc != 2)
                return; // cap exceeded OR protocol violation
        }
    };

    try {
        while (!stop_requested_.load(std::memory_order_acquire)) {
            bool did_work = false;

            // 1) Take from mailbox into local active state.
            //    Worker-LOCAL `in_flight_` is the single
            //    "am I busy?" flag; reading has_receipt_ here is
            //    forbidden because main drains it under the
            //    mailbox mutex concurrently. After publication
            //    the worker sets in_flight_=false, so "I am not
            //    busy" == "I can take new work".
            if (!in_flight_) {
                std::lock_guard<std::mutex> lk(mailbox_mutex_);
                if (mailbox_has_request_) {
                    in_flight_ = true;
                    pending_line_ = std::move(mailbox_line_);
                    pending_offset_ = 0;
                    pending_sent_at_ms_ = mailbox_sent_at_ms_;
                    pending_deadline_ms_ = mailbox_deadline_ms_;
                    mailbox_has_request_ = false;
                    mailbox_line_.clear();
                    did_work = true;
                }
            }

            // 2) Write phase. Bounded chunks per pass. On
            //    NOT_READY, break the write pass and service
            //    reads/exit/deadline. Do NOT clear pending /
            //    deadline on write completion — the request is
            //    in flight until receipt arrives.
            if (in_flight_ && stdin_ && pending_offset_ < pending_line_.size()) {
                const auto want = pending_line_.size() - pending_offset_;
                const auto n = SDL_WriteIO(stdin_, pending_line_.data() + pending_offset_, want);
                if (n > 0) {
                    pending_offset_ += n;
                    did_work = true;
                } else {
                    const auto status = SDL_GetIOStatus(stdin_);
                    if (status != SDL_IO_STATUS_NOT_READY) {
                        fail_now("Worker stdin write failed: " + std::string(SDL_GetError()));
                    }
                    // If NOT_READY, just break this pass; the
                    // next loop pass retries the SDL_WriteIO.
                }
            }

            // 3) Deadline check EVERY iteration when the request
            //    is in flight. Boundary is STRICTLY GREATER THAN
            //    pending_deadline_ms_ (matches the late-receipt
            //    check below): an elapsed time of exactly 5000 ms
            //    is still on time. Worker-LOCAL `in_flight_` is
            //    the single gate; after publication
            //    `in_flight_=false` and this block is skipped
            //    automatically.
            if (in_flight_) {
                const auto now = monotonic_ms();
                if (now > pending_deadline_ms_) {
                    fail_now("Runtime timed out during pending request "
                             "(monotonic wait_ms=" +
                             std::to_string(now - pending_sent_at_ms_) + ")");
                }
            }

            // 4) Read stdout. EOF/ERROR -> Failed. EOF preserves
            //    receipt ordering because any complete-line receipt
            //    was already published BEFORE this read pass. The
            //    actual byte/framing/cap/publish-or-violate work
            //    is delegated to the shared `consume` helper so
            //    the regular path and the bounded final-reply
            //    drain obey the same rule.
            if (stdout_ && !stop_requested_.load()) {
                for (int i = 0; i < kReadIterationsPerPass; ++i) {
                    const auto n = SDL_ReadIO(stdout_, read_chunk, sizeof(read_chunk));
                    if (n > 0) {
                        did_work = true;
                        if (consume(read_chunk, static_cast<std::size_t>(n), monotonic_ms()) != 2)
                            break;
                    } else if (n == 0) {
                        const auto status = SDL_GetIOStatus(stdout_);
                        if (status == SDL_IO_STATUS_EOF || status == SDL_IO_STATUS_ERROR) {
                            fail_now(std::string("Worker stdout closed: ") +
                                     (status == SDL_IO_STATUS_EOF ? "EOF" : "ERROR"));
                        }
                        break;
                    } else {
                        break;
                    }
                    if (stop_requested_.load())
                        break;
                }
            }

            // 5) Stderr reads. Bounded tail-append.
            if (stderr_stream_ && !stop_requested_.load()) {
                for (int i = 0; i < kStderrIterationsPerPass; ++i) {
                    const auto n = SDL_ReadIO(stderr_stream_, read_chunk, sizeof(read_chunk));
                    if (n == 0)
                        break;
                    did_work = true;
                    {
                        std::lock_guard<std::mutex> lk(stderr_mutex_);
                        stderr_tail_.append(read_chunk, n);
                        if (stderr_tail_.size() > kStderrTailBytesMax)
                            stderr_tail_.erase(0, stderr_tail_.size() - kStderrTailBytesMax);
                    }
                }
            }

            // 6) Process exit detection. SDL_WaitProcess is "not
            //    thread safe"; this is its single owner.
            if (process_ && !stop_requested_.load()) {
                int exit_code = 0;
                if (SDL_WaitProcess(process_, false, &exit_code)) {
                    exit_seen = true;
                    exit_diagnostic =
                        "Worker runtime exited (code " + std::to_string(exit_code) + ")";
                    // Bounded final-reply drain: capture any
                    // complete newline the runtime managed to
                    // flush before exiting. If the final reply
                    // arrives within budget the receipt is
                    // published and the dispatch handles the
                    // process-exit as a follow-on event (already
                    // handled by the post-dispatch take_failure
                    // path in PlaySession). If no complete line
                    // arrives, the original failure applies.
                    drain_and_publish_final();
                    fail_now(exit_diagnostic);
                }
            }

            // 7) Idle wait. Predicate is stop-only. The wait
            //    wakes on stop notification and ends the wait
            //    early; request notifications do not satisfy the
            //    predicate, so a request submitted while idle
            //    wakes the worker but the wait re-checks the
            //    predicate, sees it still false, and waits again.
            //    When idle, retry after kIdleWait. Scheduling can
            //    delay resumption beyond kIdleWait.
            if (!did_work) {
                std::unique_lock<std::mutex> lk(mailbox_mutex_);
                mailbox_cv_.wait_for(lk, kIdleWait, [this] {
                    return stop_requested_.load(std::memory_order_acquire);
                });
            }
        }
    } catch (const std::exception& e) {
        set_failure(failure_mutex_, failure_, std::string("Worker loop exception: ") + e.what());
        {
            std::lock_guard<std::mutex> lk(mailbox_mutex_);
            mailbox_busy_ = false;
        }
        stop_requested_.store(true, std::memory_order_release);
        in_flight_ = false;
    } catch (...) {
        set_failure(failure_mutex_, failure_, "Worker loop unknown exception");
        {
            std::lock_guard<std::mutex> lk(mailbox_mutex_);
            mailbox_busy_ = false;
        }
        stop_requested_.store(true, std::memory_order_release);
        in_flight_ = false;
    }

    // Shutdown: single call. try/catch covers setup / drain / kill
    // / wait / destroy so an exception cannot leave the SDL handles
    // dangling or call std::terminate.
    try {
        shutdown_and_destroy();
    } catch (const std::exception& e) {
        set_failure(failure_mutex_, failure_,
                    std::string("Worker shutdown exception: ") + e.what());
    } catch (...) {
        set_failure(failure_mutex_, failure_, "Worker shutdown unknown exception");
    }
    if (exit_seen) {
        std::lock_guard<std::mutex> lk(failure_mutex_);
        if (failure_.empty())
            failure_ = std::move(exit_diagnostic);
    }
    running_.store(false, std::memory_order_release);
    mailbox_cv_.notify_all();
}
} // namespace forge