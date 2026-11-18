// Original optional behavior for the two session-input Gallery Scenes.
#include <cstring>
#include <flecs.h>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

void forge_register_system_SessionInput(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<forge::LocalTranslation>("forge.local_translation");
    auto pending = std::make_shared<std::uint64_t>(0);
    world.observer("gameplay.GallerySessionInput")
        .event<forge::LocalTranslation>()
        .with(flecs::Any)
        .each([host, pending](flecs::iter& it, std::size_t row) {
            const auto* payload = it.param<forge::LocalTranslation>();
            const forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 source{};
            if (!payload || client.entity_reference(it.entity(row), source) != 1)
                return;
            const bool first =
                !std::strcmp(source.scene_uuid, "306f7bf0-c7cc-4733-9043-7c379d2afff2");
            if (!first && std::strcmp(source.scene_uuid, "b78ea324-266f-490e-94e1-8159d010302a"))
                return;
            const auto* cube = first ? "21f44403-4b91-466f-850b-d799abdd1fa6"
                                     : "2bad1257-336b-49fe-a400-fb653dead648";
            if (!std::strcmp(source.entity_uuid, cube)) {
                auto next = *payload;
                next.x += .5;
                it.entity(row).set<forge::LocalTranslation>(next);
                return;
            }
            const auto* trigger = first ? "348c7de9-f303-4fba-9a9d-28d44c93871c"
                                        : "6d61ef69-782b-4dc5-929b-4fbe5bf6b0cf";
            if (std::strcmp(source.entity_uuid, trigger))
                return;
            if (!client.callable(forge::sdk::Capability::Game)) {
                client.diagnostic(2, "SessionInput requires the module's Game capability");
                return;
            }
            if (*pending) {
                const auto size = host->game_inspect(host->context, *pending, nullptr, 0);
                if (!size || size > 65536) {
                    client.diagnostic(2, "SessionInput request receipt is unavailable");
                    return;
                }
                std::vector<char> bytes(size);
                if (host->game_inspect(host->context, *pending, bytes.data(), size) != size)
                    return;
                const auto receipt = nlohmann::json::parse(bytes.data());
                if (receipt.at("state").get<std::string>() == "queued")
                    return;
                if (receipt.at("state").get<std::string>() == "failed")
                    client.diagnostic(2, receipt.at("error").get<std::string>().c_str());
                if (!host->game_release(host->context, *pending))
                    return;
                *pending = 0;
            }
            const auto request = nlohmann::json{
                {"operation", "prepare"},
                {"asset", first ? "b78ea324-266f-490e-94e1-8159d010302a"
                                : "306f7bf0-c7cc-4733-9043-7c379d2afff2"},
                {"activate", true}}.dump();
            *pending = host->game_request(host->context, request.c_str());
            if (!*pending)
                client.diagnostic(2, "SessionInput Scene request was rejected");
        });
}
