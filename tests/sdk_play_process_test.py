#!/usr/bin/env python3
# Test-only driver that exercises the production SdkPlayRuntime::process()
# loop over real stdin/stdout pipes. Verifies the runtime quits
# orderly after a gameplay Quit request: the FIRST correlated response
# with quit_requested=true MUST arrive on a request id strictly
# greater than any previous id; the runtime then closes its end of
# the pipe (EOF) and exits rc=0 without emitting any unsolicited
# id=0 event.
#
# Portable: stdout is drained by a reader thread that pushes into a
# bounded Queue, so this works on POSIX anonymous pipes AND Windows.
# stderr is drained into a log file for post-mortem.
import json
import os
import queue
import subprocess
import sys
import threading
import time


def _encode(payload):
    return (json.dumps(payload) + "\n").encode("utf-8")


class ProcessBridge:
    def __init__(self, proc, stderr_log):
        self.proc = proc
        self.stderr_log = stderr_log
        self._stdout_q = queue.Queue()
        self._buf = bytearray()
        self._buf_lock = threading.Lock()
        self._closed = threading.Event()
        self._reader = threading.Thread(
            target=self._reader_thread, name="stdout-reader", daemon=True
        )
        self._stderr = threading.Thread(
            target=self._stderr_thread, name="stderr-drain", daemon=True
        )
        self._reader.start()
        self._stderr.start()

    def _reader_thread(self):
        try:
            while True:
                chunk = self.proc.stdout.read(4096)
                if not chunk:
                    self._closed.set()
                    return
                self._stdout_q.put(chunk)
        except Exception:
            self._closed.set()

    def _stderr_thread(self):
        try:
            with open(self.stderr_log, "wb") as fh:
                while True:
                    chunk = self.proc.stderr.read(4096)
                    if not chunk:
                        return
                    fh.write(chunk)
                    fh.flush()
        except Exception:
            return

    def send(self, payload):
        self.proc.stdin.write(payload)
        self.proc.stdin.flush()

    def read_message(self, deadline, expected_id=None):
        # Parse one newline-terminated JSON message. The carry-over
        # buffer is stored on the bridge so multi-line single-chunk
        # reads cannot hide a forbidden extra response behind a
        # discarded tail. Spurious empty lines are tolerated. If the
        # child closed the pipe with a non-empty, newline-free tail,
        # report it as truncation rather than spinning forever. The
        # deadline is enforced whether the child is closed or not —
        # we never busy-loop on a stuck or partial stream.
        while True:
            with self._buf_lock:
                idx = self._buf.find(b"\n")
                if idx >= 0:
                    line = bytes(self._buf[:idx])
                    del self._buf[: idx + 1]
                else:
                    line = None
            if line is not None:
                if not line.strip():
                    continue
                msg = json.loads(line.decode("utf-8"))
                if expected_id is not None and msg.get("id") != expected_id:
                    raise AssertionError(
                        f"expected id={expected_id} but received id={msg.get('id')}: {msg}"
                    )
                return msg
            # Need more bytes.
            if time.monotonic() > deadline:
                raise TimeoutError("no response within deadline")
            try:
                chunk = self._stdout_q.get(timeout=0.05)
            except queue.Empty:
                if self._closed.is_set():
                    with self._buf_lock:
                        tail = bytes(self._buf)
                        self._buf.clear()
                    if tail:
                        raise EOFError(
                            f"child closed stdout with non-empty trailing bytes "
                            f"(no newline): {tail!r}"
                        )
                    raise EOFError("child closed stdout")
                continue
            with self._buf_lock:
                self._buf.extend(chunk)

    def wait_exit(self, deadline):
        while time.monotonic() < deadline:
            rc = self.proc.poll()
            if rc is not None:
                return rc
            time.sleep(0.02)
        self.proc.kill()
        return self.proc.wait(timeout=5)

    def close(self):
        # Cleanup helper: kill only if still alive, then wait with
        # timeout, close pipe handles, and join the reader threads so
        # no log data is lost. The caller decides whether to invoke
        # this BEFORE success (no — we only do this on failure) or
        # after rc has been observed.
        if self.proc.poll() is None:
            try:
                self.proc.kill()
            except OSError:
                pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        for stream in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
            try:
                if stream:
                    stream.close()
            except OSError:
                pass
        for thread in (self._reader, self._stderr):
            thread.join(timeout=2)


def expect_ok(resp, label):
    if not resp.get("ok", False):
        raise AssertionError(f"{label} returned ok=false: {resp}")


def main():
    test_exe = sys.argv[1]
    user_base = sys.argv[2]
    stderr_log = sys.argv[3] if len(sys.argv) > 3 else os.path.join(
        os.path.dirname(user_base), "sdk-process-driver.stderr.log"
    )
    proc = subprocess.Popen(
        [test_exe, "--mode=process", user_base],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
    )
    bridge = ProcessBridge(proc, stderr_log)
    rid = 1
    try:
        # 1. hello -> session.
        bridge.send(_encode({
            "protocol": 2,
            "id": rid,
            "command": "hello",
            "application_id": "forge.test.process",
        }))
        resp = bridge.read_message(time.monotonic() + 10.0, expected_id=rid)
        expect_ok(resp, "hello")
        if resp.get("quit_requested", False):
            raise AssertionError("hello advertised quit_requested=true")
        session = resp["session"]
        rid += 1
        # 2. snapshot with editor_epoch release at epoch 1.
        bridge.send(_encode({
            "protocol": 2,
            "session": session,
            "id": rid,
            "command": "snapshot",
            "editor_epoch": {"epoch": 1, "captured": False, "input_device": "KeyboardMouse"},
        }))
        resp = bridge.read_message(time.monotonic() + 10.0, expected_id=rid)
        expect_ok(resp, "epoch snapshot")
        rid += 1
        # 3. replace -> candidate.
        bridge.send(_encode({
            "protocol": 2,
            "session": session,
            "id": rid,
            "command": "replace",
            "scene": {"version": 1,
                      "entities": [{"id": "stay",
                                    "name": "Stay",
                                    "components": {}}]},
        }))
        resp = bridge.read_message(time.monotonic() + 10.0, expected_id=rid)
        expect_ok(resp, "replace")
        rid += 1
        # 4. poll snapshot until response.candidate is an object with
        # a ticket reference; do NOT auto-activate. The runtime does
        # not auto-activate; the editor must fetch the envelope and
        # ack the candidate via `candidate_ack` on the next snapshot.
        candidate_ref = None
        for attempt in range(50):
            current_id = rid
            bridge.send(_encode({
                "protocol": 2,
                "session": session,
                "id": current_id,
                "command": "snapshot",
            }))
            resp = bridge.read_message(time.monotonic() + 10.0, expected_id=current_id)
            expect_ok(resp, f"candidate-poll #{attempt}")
            cand = resp.get("candidate")
            rid += 1
            if isinstance(cand, dict) and cand.get("ticket"):
                candidate_ref = cand
                break
        if candidate_ref is None:
            raise AssertionError(
                "Runtime did not surface a pending candidate reference within the poll window"
            )
        if candidate_ref.get("session") != session:
            raise AssertionError(
                f"candidate_ref.session mismatch: {candidate_ref}"
            )
        ticket = candidate_ref["ticket"]
        # 5. `candidate` command fetches the frozen envelope. The
        # response carries session, id, ok, candidate (full envelope).
        bridge.send(_encode({
            "protocol": 2,
            "session": session,
            "id": rid,
            "command": "candidate",
            "ticket": ticket,
        }))
        env_resp = bridge.read_message(time.monotonic() + 10.0, expected_id=rid)
        expect_ok(env_resp, "candidate fetch")
        envelope = env_resp.get("candidate")
        if not isinstance(envelope, dict):
            raise AssertionError(f"candidate envelope missing: {env_resp}")
        if envelope.get("ticket") != ticket or envelope.get("session") != session:
            raise AssertionError(
                f"envelope session/ticket mismatch: {envelope}"
            )
        rid += 1
        # 6. snapshot with candidate_ack activates the scene.
        bridge.send(_encode({
            "protocol": 2,
            "session": session,
            "id": rid,
            "command": "snapshot",
            "candidate_ack": {"session": session, "ticket": ticket,
                               "accepted": True, "diagnostic": ""},
        }))
        resp = bridge.read_message(time.monotonic() + 10.0, expected_id=rid)
        expect_ok(resp, "candidate_ack")
        rid += 1
        # 7. drain pending release offers. The host's poll_cursor_release
        # path for the upcoming gameplay Quit emits a release(false)
        # offer. We MUST ack it before the host consumes the Quit
        # request. The fixture module's controls callback runs once
        # per control_frame EVEN while the clock is paused (after
        # initial activation), so no resume is required.
        observed_release_token = None
        for attempt in range(50):
            current_id = rid
            bridge.send(_encode({
                "protocol": 2,
                "session": session,
                "id": current_id,
                "command": "snapshot",
            }))
            resp = bridge.read_message(time.monotonic() + 10.0, expected_id=current_id)
            expect_ok(resp, f"post-ack snapshot #{attempt}")
            rid += 1
            effects = resp.get("platform_effects", [])
            release_offers = []
            capture_offers = []
            for entry in effects:
                if entry.get("kind") != "cursor":
                    continue
                v = entry.get("value")
                if isinstance(v, bool) and v is False:
                    release_offers.append(entry)
                elif isinstance(v, bool) and v is True:
                    capture_offers.append(entry)
            # Editor never offered capture from this fixture; if any
            # capture offer appears it must be rejected (the editor
            # never asked for capture, so it cannot ack one).
            if capture_offers:
                raise AssertionError(
                    f"unexpected cursor capture offer: {capture_offers}"
                )
            if not release_offers:
                # No release yet; if quit_requested=true the runtime
                # is already exiting cleanly.
                if resp.get("quit_requested", False):
                    break
                continue
            # The fixture's release(false) token must appear here.
            observed_release_token = release_offers[0].get("token")
            for offer in release_offers:
                ack_id = rid
                ack = dict(offer)
                ack["session"] = session
                ack["accepted"] = True
                ack["diagnostic"] = ""
                bridge.send(_encode({
                    "protocol": 2,
                    "session": session,
                    "id": ack_id,
                    "command": "snapshot",
                    "platform_acks": [ack],
                }))
                rid += 1
                ack_resp = bridge.read_message(
                    time.monotonic() + 10.0, expected_id=ack_id
                )
                expect_ok(ack_resp, "release ack")
                if ack_resp.get("quit_requested", False):
                    break
            break
        if observed_release_token is None:
            raise AssertionError(
                "Runtime did not surface a cursor release(false) offer"
            )
        # 8. poll snapshot for the FIRST quit_requested=true response.
        # The fixture module's controls callback runs once per
        # control_frame and requests gameplay Quit after the first
        # invocation (quit_after_control_count=1). The host consumes
        # the Quit request on the next pump, latches quit_latched,
        # and the next correlated snapshot carries quit_requested=true.
        # On that first quit_requested=true response we MUST stop
        # sending commands. The runtime will close its stdout end of
        # the pipe after that response is flushed; EOF + rc=0 is the
        # success condition. Do NOT close parent stdin first.
        observed_quit = False
        first_quit_id = None
        for attempt in range(500):
            bridge.send(_encode({
                "protocol": 2,
                "session": session,
                "id": rid,
                "command": "snapshot",
            }))
            current_id = rid
            rid += 1
            try:
                resp = bridge.read_message(time.monotonic() + 10.0,
                                           expected_id=current_id)
            except (EOFError, TimeoutError) as exc:
                # Runtime may have closed mid-pump after latching quit
                # if no further correlated requests were available;
                # however the production process loop always answers
                # one more correlated request. A timeout means the
                # runtime stopped answering — surface the failure.
                raise AssertionError(
                    f"runtime stopped answering after attempt {attempt}: {exc}"
                )
            if resp.get("id", 0) == 0:
                raise AssertionError(
                    f"unsolicited id=0 response: {resp}"
                )
            if resp.get("quit_requested", False):
                observed_quit = True
                first_quit_id = resp["id"]
                break
        if not observed_quit:
            raise AssertionError(
                "Runtime did not advertise quit_requested=true within the poll window"
            )
        # 9. The runtime must close stdout and exit rc=0 after the
        # correlated quit_requested=true response. Do not send any
        # more commands and do not close parent stdin first.
        try:
            extra = bridge.read_message(time.monotonic() + 5.0)
            raise AssertionError(
                f"unexpected extra response after quit_requested=true: {extra}"
            )
        except EOFError:
            pass
        rc = bridge.wait_exit(time.monotonic() + 10.0)
        if rc != 0:
            raise AssertionError(
                f"Runtime exited with code {rc} after quit_requested=true; see {stderr_log}"
            )
        print(f"process-mode quit-latch driver OK "
              f"(first_quit_id={first_quit_id}, release_token={observed_release_token})",
              flush=True)
    finally:
        # Cleanup: kill only if the child is still alive, always wait
        # with a bounded timeout, close pipe handles, and join the
        # reader/log threads so no stderr data is lost. Do NOT force
        # stdin closed before the orderly-exit path has actually
        # succeeded — closing stdin would itself be the EOF the test
        # is trying to observe.
        bridge.close()


# --- Direct ProcessBridge regressions --------------------------------
#
# These exercise the bridge on synthetic byte sources so we can prove
# the multi-line single-chunk carry-over path and the partial-line +
# EOF truncation path without spawning the runtime. The driver path
# already exercises the bridge end-to-end against the production
# binary, so these unit checks only cover the cases that are awkward
# to reproduce against the live runtime.

class _FakeStream:
    def __init__(self, chunks):
        self._chunks = list(chunks)

    def read(self, n):
        if not self._chunks:
            return b""
        return self._chunks.pop(0)


def _bridge_with(fixed_stderr_path):
    """Build a bridge backed by fake streams so we can feed scripted
    byte sequences and observe the bridge's parsing/EOF behavior."""
    proc = type("P", (), {})()
    proc.stdin = type("S", (), {"write": lambda self, b: None,
                                 "flush": lambda self: None,
                                 "close": lambda self: None})()
    proc.stdout = _FakeStream([])
    proc.stderr = _FakeStream([])
    proc.poll = lambda: None
    proc.kill = lambda: None
    proc.wait = lambda timeout=5: 0
    bridge = ProcessBridge.__new__(ProcessBridge)
    bridge.proc = proc
    bridge.stderr_log = fixed_stderr_path
    bridge._stdout_q = queue.Queue()
    bridge._buf = bytearray()
    bridge._buf_lock = threading.Lock()
    bridge._closed = threading.Event()
    bridge._reader = threading.Thread(target=lambda: None, daemon=True)
    bridge._stderr = threading.Thread(target=lambda: None, daemon=True)
    return bridge


def regression_multiline_single_chunk_carryover():
    """A single 4 KiB read that contains TWO newline-terminated JSON
    messages must surface each on its own call; the carry-over buffer
    must NOT discard the second line. This proves the local-buf bug
    cannot hide a forbidden extra response."""
    bridge = _bridge_with("/tmp/sdk_dummy_stderr.log")
    payload = (
        json.dumps({"id": 1, "ok": True, "marker": "first"}) + "\n"
        + json.dumps({"id": 2, "ok": True, "marker": "second"}) + "\n"
    ).encode("utf-8")
    bridge._stdout_q.put(payload)
    bridge._closed.set()
    first = bridge.read_message(time.monotonic() + 1.0, expected_id=1)
    check_equal(first["marker"], "first", "first multiline message")
    second = bridge.read_message(time.monotonic() + 1.0, expected_id=2)
    check_equal(second["marker"], "second", "second multiline message carried over")


def regression_partial_line_eof_raises_truncation():
    """A child close with a non-empty, newline-free tail MUST raise
    EOFError with a truncation message, not spin or silently return
    garbage. The deadline must still be honoured even when the child
    is closed."""
    bridge = _bridge_with("/tmp/sdk_dummy_stderr.log")
    bridge._stdout_q.put(b"{partial-no-newline")
    bridge._closed.set()
    try:
        bridge.read_message(time.monotonic() + 1.0)
        raise AssertionError(
            "expected EOFError for partial-line + EOF, but got a message"
        )
    except EOFError as exc:
        if "non-empty trailing bytes" not in str(exc):
            raise AssertionError(
                f"EOFError missing truncation diagnostic: {exc}"
            ) from exc


def regression_deadline_honoured_on_idle_child():
    """If the child is closed and the carry-over buffer is empty,
    read_message must raise EOFError promptly. If the child is alive
    but no data arrives, read_message must raise TimeoutError after
    the deadline — never busy-loop."""
    bridge = _bridge_with("/tmp/sdk_dummy_stderr.log")
    # Closed + empty -> EOFError quickly.
    bridge._closed.set()
    start = time.monotonic()
    try:
        bridge.read_message(start + 2.0)
        raise AssertionError("expected EOFError on closed-empty bridge")
    except EOFError:
        elapsed = time.monotonic() - start
        if elapsed > 0.5:
            raise AssertionError(
                f"closed-empty EOF took {elapsed:.3f}s; busy-loop suspected"
            ) from None
    # Alive + no data -> TimeoutError after deadline.
    bridge2 = _bridge_with("/tmp/sdk_dummy_stderr.log")
    start = time.monotonic()
    try:
        bridge2.read_message(start + 0.3)
        raise AssertionError("expected TimeoutError on idle child")
    except TimeoutError:
        elapsed = time.monotonic() - start
        if elapsed > 1.5:
            raise AssertionError(
                f"idle TimeoutError took {elapsed:.3f}s; deadline not enforced"
            ) from None


def check_equal(actual, expected, label):
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


if __name__ == "__main__":
    if "--bridge-regression" in sys.argv:
        regression_multiline_single_chunk_carryover()
        regression_partial_line_eof_raises_truncation()
        regression_deadline_honoured_on_idle_child()
        print("bridge regressions OK", flush=True)
        sys.exit(0)
    main()