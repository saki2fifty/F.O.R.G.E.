#pragma once
#include <forge/engine_assets.hpp>
#include <forge/sdk_client.hpp>
#include <forge/services.hpp>
#include <future>
inline void test_sdk_resources(const ForgeSdkWorldV1* host, forge::ServiceAccess services) {
    using namespace forge;
    using namespace std::chrono_literals;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    sdk::Client client(host);
    check(client.callable(sdk::Capability::Resources), "SDK resource service unavailable");
    check(!(client.query(sdk::Capability::Resources).flags & FORGE_SDK_FIXED_ONLY),
          "CPU resources cannot progress while paused");
    const auto cube = engine_primitive(0).id.str();
    auto token = client.request_resource(FORGE_SDK_RESOURCE_MESH, cube.c_str());
    check(token != 0, "SDK builtin mesh request rejected");
    ForgeSdkResourceV1 info{};
    const auto end = std::chrono::steady_clock::now() + 5s;
    do {
        services.resources()->synchronize();
        check(client.inspect_resource(token, info), "SDK cannot inspect own resource");
        check(std::chrono::steady_clock::now() < end, "SDK resource never became ready");
        std::this_thread::sleep_for(1ms);
    } while (std::string(info.state) != "ready" || !info.retained_revision[0]);
    check(std::string(info.requested_revision) == info.retained_revision &&
              std::strlen(info.retained_revision) == 64 && info.source_generation,
          "SDK did not copy revision provenance");
    auto foreign = services.resources()->request(RuntimeResourceKind::Mesh, engine_primitive(0).id);
    check(!client.inspect_resource(foreign, info) && !info.state[0] &&
              !client.release_resource(foreign),
          "SDK accepted another module's resource token");
    services.resources()->release(foreign);
    check(!client.request_resource(FORGE_SDK_RESOURCE_MATERIAL, cube.c_str()) &&
              !client.request_resource(99, cube.c_str()) &&
              !client.request_resource(FORGE_SDK_RESOURCE_MESH, "invalid") &&
              !client.request_resource(FORGE_SDK_RESOURCE_MESH, cube.c_str(), 2) &&
              !client.request_resource(FORGE_SDK_RESOURCE_TEXTURE, cube.c_str(), 99),
          "SDK accepted wrong asset type or malformed request");
    info.size = sizeof(info) - 1;
    check(!host->resource_inspect(host->context, token, &info), "SDK accepted wrong output size");
    check(std::async(std::launch::async,
                     [&] {
                         ForgeSdkResourceV1 out{};
                         return !client.inspect_resource(token, out) &&
                                !client.release_resource(token) &&
                                !client.request_resource(FORGE_SDK_RESOURCE_MESH, cube.c_str());
                     })
              .get(),
          "SDK resources allowed a foreign thread");
    check(client.release_resource(token) && !client.release_resource(token) &&
              !client.inspect_resource(token, info),
          "SDK stale token accepted");
    std::vector<uint64_t> tokens;
    for (unsigned i = 0; i < 64; ++i) {
        auto requested = client.request_resource(FORGE_SDK_RESOURCE_MESH, cube.c_str());
        check(requested != 0, "SDK bounded subscriptions rejected too early");
        tokens.push_back(requested);
    }
    check(!client.request_resource(FORGE_SDK_RESOURCE_MESH, cube.c_str()),
          "SDK module subscription budget not enforced");
    for (unsigned i = 0; i < 63; ++i)
        client.release_resource(tokens[i]);
    // Leave one owned subscription: world/module shutdown must release it safely.
}
