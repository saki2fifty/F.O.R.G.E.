#include <forge/game_control_queue.hpp>
#include <iostream>
#include <thread>

using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void requests() {
    GameControlQueue host;
    auto world = host.world(), candidate = host.world();
    rejects([&] { world->request("game.test", {{"operation", "pause"}}); });
    host.activate(world);
    host.status({{"state", "running"}});
    auto copy = world->query();
    copy["state"] = "tampered";
    check(world->query().at("state") == "running", "Status was not copied");
    rejects([&] { candidate->request("game.test", {{"operation", "pause"}}); });
    auto token = world->request("game.test", {{"operation", "pause"}});
    check(world->inspect("game.test", token).state == "queued", "Request executed inline");
    rejects([&] { world->inspect("other.module", token); });
    rejects([&] { world->release("other.module", token); });
    bool foreign_rejected = false;
    std::thread foreign([&] {
        try {
            world->query();
        } catch (...) {
            foreign_rejected = true;
        }
    });
    foreign.join();
    check(foreign_rejected, "Foreign thread accessed game service");
    unsigned calls = 0;
    host.drain([&](const Json& command, const GameSaveSchema*, const std::string&) {
        ++calls;
        rejects([&] {
            host.drain(
                [](const Json&, const GameSaveSchema*, const std::string&) { return Json(); });
        });
        return Json{{"performed", command.at("operation")}};
    });
    check(calls == 1 && world->inspect("game.test", token).state == "succeeded",
          "Deferred request did not complete");
    world->release("game.test", token);
    rejects([&] { world->inspect("game.test", token); });
    token = world->request("game.test", {{"operation", "load"}});
    host.drain([](const Json&, const GameSaveSchema*, const std::string&) -> Json {
        throw std::runtime_error("corrupt save");
    });
    check(world->inspect("game.test", token).state == "failed" &&
              world->inspect("game.test", token).error == "corrupt save",
          "Operation failure lost diagnostic");
    world->release("game.test", token);
    world->request("game.test", {{"operation", "pause"}});
    world->revoke("game.test");
    host.drain([&](const Json&, const GameSaveSchema*, const std::string&) {
        ++calls;
        return Json();
    });
    check(calls == 1, "Revoked module's queued work executed");
    rejects([&] { world->request("game.test", {{"operation", "save"}}); });
    auto old = world->request("game.next", {{"operation", "prepare"}});
    world->request("game.next", {{"operation", "save"}});
    host.drain([&](const Json&, const GameSaveSchema*, const std::string&) {
        ++calls;
        host.activate(candidate);
        return Json();
    });
    check(calls == 2, "Retired world's pending work survived scene switch");
    rejects([&] { world->inspect("game.test", old); });
    rejects([&] { world->request("game.test", {{"operation", "save"}}); });
    auto next = candidate->request("game.test", {{"operation", "pause"}});
    check(next != old, "World switch reused token identity");
    candidate->release("game.test", next);
    for (unsigned i = 0; i < 128; ++i)
        candidate->request("game.test", {{"operation", "pause"}});
    rejects([&] { candidate->request("game.test", {{"operation", "pause"}}); });
    rejects([&] { candidate->request("game.test", {{"operation", "arbitrary_native_code"}}); });
}
void callback_lifetime() {
    GameControlQueue host;
    auto world = host.world();
    host.activate(world);
    bool code_destroyed = false;
    auto code = std::shared_ptr<void>(new int(1), [&](void* p) {
        delete static_cast<int*>(p);
        code_destroyed = true;
    });
    unsigned validations = 0;
    GameSaveSchema schema;
    schema.validate = [&](const GameSave&) {
        check(!code_destroyed, "Validator executed after code retirement");
        ++validations;
    };
    world->save_schema("game.test", schema, code);
    code.reset();
    world->request("game.test", {{"operation", "save"}});
    host.drain([&](const Json&, const GameSaveSchema* active, const std::string&) {
        check(active != nullptr, "Registered save schema missing");
        world->revoke("game.test");
        check(!code_destroyed, "Revocation unloaded code during callback execution");
        active->validate({AssetId::generate(), Json::object()});
        return Json();
    });
    check(code_destroyed && validations == 1, "Save callback lease was not released after return");
    world->save_schema("game.next", schema, {});
    world->request("game.next", {{"operation", "save"}});
    world->revoke("game.next");
    bool executed = false;
    host.drain([&](const Json&, const GameSaveSchema*, const std::string&) -> Json {
        executed = true;
        return Json();
    });
    check(!executed && validations == 1, "Revoked validator executed later");
    std::shared_ptr<GameControlService> stale;
    {
        GameControlQueue transient;
        stale = transient.world();
        transient.activate(stale);
    }
    rejects([&] { stale->query(); });
}
void poll_pending() {
    GameControlQueue host;
    auto world = host.world();
    host.activate(world);
    // Empty callback rejection (poll and drain both reject empty callbacks
    // even when there is nothing pending).
    rejects([&] { host.poll(GameControlQueue::Poll{}); });
    rejects([&] { host.drain(GameControlQueue::Execute{}); });
    // Pending across multiple pumps: callback returns nullopt first call,
    // then a Json second. Explicit JSON null success is distinct from nullopt.
    auto token = world->request("game.test", {{"operation", "cursor"}});
    unsigned calls = 0;
    host.poll([&](std::uint64_t seen, const Json& command, const GameSaveSchema*,
                  const std::string& module) -> std::optional<Json> {
        ++calls;
        check(seen == token, "Token mismatch on first pump");
        check(command.at("operation") == "cursor", "Wrong command on first pump");
        check(module == "game.test", "Wrong module on first pump");
        return std::nullopt;
    });
    check(calls == 1 && world->inspect("game.test", token).state == "queued",
          "First pump should keep entry queued");
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++calls;
        check(seen == token, "Token mismatch on second pump");
        return Json{{"ack", seen}};
    });
    check(calls == 2, "Second pump did not invoke callback");
    check(world->inspect("game.test", token).state == "succeeded" &&
              world->inspect("game.test", token).value.at("ack") == token,
          "Second pump did not complete with payload");
    world->release("game.test", token);
    // Subsequent pump: completed entry must not be invoked again.
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++calls;
        return Json::object();
    });
    check(calls == 2, "Completed entry was re-executed on later pump");
    // Explicit JSON null success: optional<Json{null}> is distinct from nullopt.
    auto null_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll(
        [&](std::uint64_t, const Json&, const GameSaveSchema*,
            const std::string&) -> std::optional<Json> { return std::optional<Json>(Json()); });
    auto null_status = world->inspect("game.test", null_token);
    check(null_status.state == "succeeded" && null_status.value.is_null(),
          "Explicit JSON null was treated as missing result");
    world->release("game.test", null_token);
    // Oversized result must become a failed receipt, not an exception.
    auto big_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        Json huge = Json::object();
        Json filler = Json::array();
        std::string chunk(1024, 'x');
        while (filler.dump().size() < 1024 * 1100)
            filler.push_back(chunk);
        huge["big"] = filler;
        return std::optional<Json>(huge);
    });
    auto big_status = world->inspect("game.test", big_token);
    check(big_status.state == "failed" && big_status.error == "Game operation result exceeds 1 MiB",
          "Oversized result did not produce a failed receipt");
    bool big_serializable = false;
    try {
        (void)big_status.value.dump();
        big_serializable = true;
    } catch (...) {
    }
    check(big_serializable, "Failed oversized receipt value was not safely serializable");
    world->release("game.test", big_token);
    // Subsequent request must still be admitted after a failed callback.
    auto post_big_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> { return Json::object(); });
    check(world->inspect("game.test", post_big_token).state == "succeeded",
          "Queue did not admit new request after an oversized failure");
    world->release("game.test", post_big_token);
    // Invalid UTF-8 in a returned Json must become a failed receipt, not
    // escape the host pump. Single-byte invalid lead byte is sufficient.
    auto bad_utf_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        Json bad = Json::object();
        bad["bad"] = std::string("\xc3\x28", 2);
        return std::optional<Json>(bad);
    });
    auto bad_utf_status = world->inspect("game.test", bad_utf_token);
    check(bad_utf_status.state == "failed", "Invalid UTF-8 did not produce a failed receipt");
    check(!bad_utf_status.error.empty(), "Invalid UTF-8 receipt lost its diagnostic error");
    bool bad_serializable = false;
    try {
        (void)bad_utf_status.value.dump();
        bad_serializable = true;
    } catch (...) {
    }
    check(bad_serializable, "Invalid UTF-8 receipt value was not safely serializable");
    world->release("game.test", bad_utf_token);
    // Subsequent request must still be admitted after a serialization
    // failure; the queue must not have lost its dispatching ability.
    auto post_bad_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> { return Json{{"ok", true}}; });
    check(world->inspect("game.test", post_bad_token).state == "succeeded" &&
              world->inspect("game.test", post_bad_token).value.at("ok") == true,
          "Queue did not admit new request after a serialization failure");
    world->release("game.test", post_bad_token);
    // Exception failure: callback throws.
    auto err_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        throw std::runtime_error("adapter offline");
    });
    auto err_status = world->inspect("game.test", err_token);
    check(err_status.state == "failed" && err_status.error == "adapter offline",
          "Thrown exception was not mapped to failed receipt");
    world->release("game.test", err_token);
    // Non-std exception also becomes a failed receipt.
    auto int_token = world->request("game.test", {{"operation", "cursor"}});
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> { throw 42; });
    auto int_status = world->inspect("game.test", int_token);
    check(int_status.state == "failed" && int_status.error == "Game operation callback failed",
          "Non-std exception did not produce a failed receipt");
    world->release("game.test", int_token);
    // Reentrant poll on owner thread must throw.
    auto token_r = world->request("game.test", {{"operation", "pause"}});
    bool nested_rejected = false;
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        try {
            host.poll([](std::uint64_t, const Json&, const GameSaveSchema*,
                         const std::string&) -> std::optional<Json> { return Json::object(); });
        } catch (const std::logic_error&) {
            nested_rejected = true;
        }
        return Json::object();
    });
    check(nested_rejected, "Reentrant poll was not rejected");
    world->release("game.test", token_r);
    // Foreign thread: poll from a non-owner thread must throw.
    auto token_f = world->request("game.test", {{"operation", "pause"}});
    bool foreign_rejected = false;
    std::thread foreign([&] {
        try {
            host.poll([](std::uint64_t, const Json&, const GameSaveSchema*,
                         const std::string&) -> std::optional<Json> { return Json::object(); });
        } catch (const std::exception&) {
            foreign_rejected = true;
        }
    });
    foreign.join();
    check(foreign_rejected, "Foreign thread polled game queue");
    world->release("game.test", token_f);
    // Endpoint retirement while pending invalidates the entry.
    auto stale = host.world();
    host.activate(stale);
    stale->request("game.test", {{"operation", "cursor"}});
    unsigned stale_calls = 0;
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++stale_calls;
        host.retire(stale);
        return std::nullopt;
    });
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++stale_calls;
        return Json::object();
    });
    check(stale_calls == 1, "Retired endpoint entry executed after retirement");
    rejects([&] { stale->request("game.test", {{"operation", "cursor"}}); });
    auto world2 = host.world();
    host.activate(world2);
    world2->request("game.test", {{"operation", "cursor"}});
    unsigned revoke_calls = 0;
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++revoke_calls;
        world2->revoke("game.test");
        return std::nullopt;
    });
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++revoke_calls;
        return Json::object();
    });
    check(revoke_calls == 1, "Revoked module entry executed after revoke");
    rejects([&] { world2->request("game.test", {{"operation", "cursor"}}); });
    // Release while pending drops the entry silently.
    auto world3 = host.world();
    host.activate(world3);
    world3->request("game.test", {{"operation", "cursor"}});
    unsigned release_calls = 0;
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string& module) -> std::optional<Json> {
        ++release_calls;
        world3->release(module, seen);
        return std::nullopt;
    });
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++release_calls;
        return Json::object();
    });
    check(release_calls == 1, "Released entry was re-queued after release");
    // Token separation: different requests get monotonically increasing tokens.
    auto a = world3->request("game.test", {{"operation", "pause"}});
    auto b = world3->request("game.test", {{"operation", "pause"}});
    check(b > a, "Tokens are not monotonically increasing");
    // Callback enqueues a new entry inside the first pump; the new entry
    // must wait for the next pump. The first pump only invokes the
    // callback once for the original entry.
    auto world4 = host.world();
    host.activate(world4);
    auto head = world4->request("game.test", {{"operation", "pause"}});
    unsigned head_calls = 0;
    std::uint64_t new_token = 0;
    host.poll([&](std::uint64_t, const Json&, const GameSaveSchema*,
                  const std::string& module) -> std::optional<Json> {
        ++head_calls;
        new_token = world4->request(module, {{"operation", "pause"}});
        return Json{{"head", true}};
    });
    check(head_calls == 1, "Newly queued work was processed in the same poll as the head entry");
    check(world4->inspect("game.test", head).state == "succeeded", "Head entry did not complete");
    check(new_token != 0, "Callback did not enqueue a new request");
    // Next pump processes the new entry; the deferred head is not re-run.
    unsigned next_calls = 0;
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        ++next_calls;
        check(seen == new_token, "Next pump did not pick up the newly queued entry first");
        return Json{{"next", seen}};
    });
    check(next_calls == 1, "Next pump processed more than one entry (head was re-run)");
    world4->release("game.test", head);
    world4->release("game.test", new_token);
    // Queue 256 bound via tombstones: keep the live head, enqueue+release
    // 255 other requests so tombstones fill the pending queue without
    // hitting the requests-map limit. In head callback attempt to enqueue:
    // must reject because the in-flight slot is reserved.
    auto world5 = host.world();
    host.activate(world5);
    auto cap_token = world5->request("game.test", {{"operation", "cursor"}});
    for (unsigned i = 0; i < 255; ++i) {
        auto tomb = world5->request("game.test", {{"operation", "cursor"}});
        world5->release("game.test", tomb);
    }
    // pending.size() == 256 (head + 255 tombstones), requests.size() == 1.
    unsigned cap_calls = 0;
    bool in_flight_reserved = false;
    bool request_rejected = false;
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string& module) -> std::optional<Json> {
        ++cap_calls;
        check(seen == cap_token, "Head must be processed before the released tombstones");
        try {
            world5->request(module, {{"operation", "cursor"}});
        } catch (const std::exception&) {
            request_rejected = true;
        }
        in_flight_reserved = true;
        return std::nullopt;
    });
    check(in_flight_reserved, "Head callback never ran");
    check(cap_calls == 1, "Multiple entries executed in a single poll of one live request");
    check(request_rejected, "In-flight slot was not reserved against new requests");
    // Drain tombstones in the same poll: they have no requests map entry so
    // they are skipped silently. Head is requeued.
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        if (seen == cap_token)
            ++cap_calls;
        return std::nullopt;
    });
    // Now tombstones are flushed. The head remains pending. New enqueue is
    // accepted (no in-flight, tombstones cleared).
    auto recover_token = world5->request("game.test", {{"operation", "cursor"}});
    check(recover_token != 0, "Queue did not admit new entry after tombstones drained");
    world5->release("game.test", recover_token);
    // Drain the head so the live entry eventually completes.
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string&) -> std::optional<Json> {
        if (seen == cap_token)
            return Json::object();
        return std::nullopt;
    });
    world5->release("game.test", cap_token);
    // Normal synchronous drain regression: drain() still completes all
    // pending entries in one call when no callback returns nullopt.
    auto world6 = host.world();
    host.activate(world6);
    auto d1 = world6->request("game.test", {{"operation", "pause"}});
    auto d2 = world6->request("game.test", {{"operation", "pause"}});
    unsigned sync_calls = 0;
    host.drain([&](const Json&, const GameSaveSchema*, const std::string&) -> Json {
        ++sync_calls;
        return Json();
    });
    check(sync_calls == 2 && world6->inspect("game.test", d1).state == "succeeded" &&
              world6->inspect("game.test", d2).state == "succeeded",
          "Synchronous drain regression: not all entries completed");
    world6->release("game.test", d1);
    world6->release("game.test", d2);
    // Cross-module fairness regression: module A's request returns nullopt
    // while module B's request completes in the same poll. Both must be
    // invoked exactly once; A stays queued, B succeeds. The next poll
    // completes A without re-executing B.
    auto world7 = host.world();
    host.activate(world7);
    auto a_token = world7->request("game.a", {{"operation", "pause"}});
    auto b_token = world7->request("game.b", {{"operation", "pause"}});
    std::uint64_t seen_a = 0, seen_b = 0;
    unsigned fairness_calls = 0;
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string& module) -> std::optional<Json> {
        ++fairness_calls;
        if (module == "game.a") {
            seen_a = seen;
            return std::nullopt;
        }
        seen_b = seen;
        return Json{{"b", true}};
    });
    check(fairness_calls == 2, "Fairness poll did not invoke both callbacks exactly once");
    check(world7->inspect("game.a", a_token).state == "queued",
          "Module A entry did not stay queued after nullopt");
    check(world7->inspect("game.b", b_token).state == "succeeded" &&
              world7->inspect("game.b", b_token).value.at("b") == true,
          "Module B entry did not complete in same poll");
    unsigned fairness_completion_calls = 0;
    host.poll([&](std::uint64_t seen, const Json&, const GameSaveSchema*,
                  const std::string& module) -> std::optional<Json> {
        ++fairness_completion_calls;
        check(module == "game.a", "Second poll re-executed the already-completed module B entry");
        check(seen == a_token, "Second poll processed a different token");
        return Json{{"a", true}};
    });
    check(fairness_completion_calls == 1, "Second poll processed more than one entry");
    check(world7->inspect("game.a", a_token).state == "succeeded" &&
              world7->inspect("game.a", a_token).value.at("a") == true,
          "Module A did not complete on second poll");
    world7->release("game.a", a_token);
    world7->release("game.b", b_token);
}
} // namespace
int main() {
    try {
        requests();
        callback_lifetime();
        poll_pending();
        std::cout << "Scoped game requests and callback retirement passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
