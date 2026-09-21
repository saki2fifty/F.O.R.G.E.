#pragma once
#include "../src/runtime_entity_creation.hpp"
#include <forge/sdk_client.hpp>
#include <forge/world.hpp>
#include <future>
inline void test_sdk_entities(const ForgeSdkWorldV1* host, forge::WorldContext& world) {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    sdk::Client client(host);
    check(client.callable(sdk::Capability::RuntimeEntities) &&
              !(client.query(sdk::Capability::RuntimeEntities).flags & FORGE_SDK_FIXED_ONLY),
          "Runtime entity admission unavailable at startup/paused boundary");
    check(!client.request_entity("") && !client.request_entity("Valid", "invalid") &&
              !client.request_entity(std::string(256, 'x').c_str()) &&
              !client.request_entity("\xff"),
          "SDK invalid entity request accepted");
    const auto token = client.request_entity("Cancelled SDK request");
    ForgeSdkEntityV1 info{};
    check(token && client.inspect_entity(token, info) && info.state == FORGE_SDK_ENTITY_PENDING &&
              !info.native_entity && EntityId::parse(info.entity_uuid),
          "SDK pending identity missing");
    info.size = sizeof(info) - 1;
    check(!host->entity_inspect(host->context, token, &info), "Wrong entity output size accepted");
    check(!host->entity_request(nullptr, nullptr, "Invalid") &&
              !host->entity_release(nullptr, token),
          "Null entity context accepted");
    const auto foreign = detail::request_runtime_entity(world, "project.other", {}, "Foreign");
    check(!client.inspect_entity(foreign, info) && !info.native_entity &&
              !client.release_entity(foreign),
          "SDK stole another module's entity request");
    detail::release_runtime_entity(world, "project.other", foreign);
    check(std::async(std::launch::async,
                     [&] {
                         ForgeSdkEntityV1 out{};
                         return !client.callable(sdk::Capability::RuntimeEntities) &&
                                !client.request_entity("Wrong thread") &&
                                !client.inspect_entity(token, out) && !client.release_entity(token);
                     })
              .get(),
          "SDK entity callback accepted a foreign thread");
    check(client.release_entity(token) && !client.release_entity(token) &&
              !client.inspect_entity(token, info),
          "SDK accepted released entity token");
}
