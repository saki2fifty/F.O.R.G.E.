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
} // namespace
int main() {
    try {
        requests();
        callback_lifetime();
        std::cout << "Scoped game requests and callback retirement passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
