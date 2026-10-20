#pragma once
#include "runtime_tile_map.hpp"
#include "sdk_play_runtime.hpp"
#include "tile_map_play_receiver.hpp"
#include <forge/game_settings.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <thread>

// Real published assets, actual SDK host assembly, and its ordinary copied wire
// commands. The enclosing TileAssets fixture owns publication and cleanup.
inline void test_tile_play_runtime(const std::filesystem::path& root, forge::Json document) {
    using namespace forge;
    using namespace std::chrono_literals;
    auto check = [](bool ok, const std::string& why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    WorldContext author;
    Scene source(author);
    const auto schema = source.schema();
    for (auto& entity : document.at("entities"))
        for (const auto& type : schema.at("components")) {
            const auto name = type.at("id").get<std::string>();
            if (name == "forge.local_translation" || name == "forge.local_rotation" ||
                name == "forge.local_scale")
                for (const auto& field : type.at("fields"))
                    entity["components"][name][field.at("id").get<std::string>()] =
                        field.at("default");
        }
    WorldContext* prepared_world = nullptr;
    EngineModule module;
    module.id = "project.tile_play_test";
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.scene_ready = [&](ModuleContext& c) { prepared_world = c.owner; };
    SdkPlayRuntime::Config config;
    config.project = root;
    config.user_base = root / "users";
    config.modules.push_back(std::move(module));
    config.defaults =
        default_game_settings("org.forge.tiles-" + AssetId::generate().str(), "Tiles");
    SdkPlayRuntime host(std::move(config));
    const auto now = RuntimeClock::Time{};
    auto hello = host.handle({{"protocol", 2}, {"id", 1u}, {"command", "hello"}}, now);
    check(hello.value("ok", false) && hello.at("runtime_contract").at("tile_projection") == 1,
          "SDK host did not advertise copied tile presentation");
    const auto session = hello.at("session");
    std::uint64_t id = 1;
    auto send = [&](Json request) {
        request["protocol"] = 2;
        request["session"] = session;
        request["id"] = ++id;
        auto response = host.handle(request, now);
        check(response.value("ok", false), "Tile wire request failed: " + response.dump());
        return response;
    };
    auto prepare = [&]() {
        send({{"command", "replace"}, {"scene", document}});
        const auto end = std::chrono::steady_clock::now() + 15s;
        std::optional<Json> candidate;
        do {
            host.pump(now);
            candidate = host.pending_candidate();
            check(std::chrono::steady_clock::now() < end, "Tile candidate preparation timed out");
            if (!candidate)
                std::this_thread::sleep_for(1ms);
        } while (!candidate);
        auto response = send({{"command", "candidate"}, {"ticket", candidate->at("ticket")}});
        check(response.at("candidate") == *candidate,
              "Candidate fetch changed frozen tile envelope");
        return *candidate;
    };
    auto transfer = [&](TileMapPlayReceiver& receiver) {
        std::size_t requests = 0;
        while (auto request = receiver.next_request()) {
            check(++requests < 1024, "Tile wire transfer failed to terminate");
            auto response = send(*request);
            check(!response.contains("scene") && response.dump().size() < 300 * 1024,
                  "Tile packet inlined an active Scene or exceeded its page profile");
            receiver.receive(response.at("tile_projection"));
        }
        check(!receiver.pending() && receiver.diagnostic().empty(),
              "Tile wire transfer failed: " + receiver.diagnostic());
    };
    Json presentation;
    auto render = [&](const TileMapPlayReceiver& receiver) {
        auto scene = extract_render_scene(presentation);
        receiver.bind(scene);
        check(scene.tile_maps.size() == 2, "Tile wire fixture lost its independent instances");
        return scene;
    };
    auto candidate = prepare();
    presentation = candidate.at("scene");
    TileMapPlayReceiver receiver;
    receiver.observe(candidate.at("tile_maps"));
    auto pending = render(receiver);
    check(pending.tile_maps[0].runtime_instance && !pending.tile_maps[0].cells,
          "Unreceived candidate fell back to authored map cells");
    transfer(receiver);
    const auto frozen = render(receiver);
    check(frozen.tile_maps[0].cells && frozen.tile_maps[1].cells,
          "Prepared SDK tile instances were not copied before activation");
    const auto target = document.at("entities")[0].at("id").get<EntityId>();
    const auto scene_id = document.at("asset_id").get<AssetId>();
    const auto before = detail::runtime_tile_map_snapshot(*prepared_world, {scene_id, target});
    check(bool(before), "Prepared SDK map did not have a native immutable view");
    send({{"command", "snapshot"},
          {"candidate_ack",
           {{"session", session},
            {"ticket", candidate.at("ticket")},
            {"accepted", true},
            {"diagnostic", ""}}}});
    host.pump(now);
    auto active = send({{"command", "snapshot"}});
    check(active.at("activation").at("generation") == candidate.at("ticket"),
          "Tile candidate did not activate its exact copied generation");
    receiver.observe(active.at("tile_maps"));
    check(!receiver.next_request(), "Activation discarded the acknowledged candidate baseline");
    const std::array<TileMapCellEdit, 1> edit{{{1, {123, 456}, {2, 0}}}};
    detail::request_runtime_tile_cells(*prepared_world, "project.tile_play_test",
                                       {scene_id, target}, before->generation, edit);
    // Publication is the existing fixed-tick path; snapshots alone cannot edit.
    active = send({{"command", "snapshot"}});
    receiver.observe(active.at("tile_maps"));
    check(!receiver.next_request(), "Paused observation published a queued gameplay edit");
    send({{"command", "snapshot"},
          {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    active = send({{"command", "step"}});
    receiver.observe(active.at("tile_maps"));
    transfer(receiver);
    const auto changed = render(receiver);
    const auto cell = [&](const RenderScene& scene, EntityId actor) {
        for (const auto& map : scene.tile_maps)
            if (map.entity == actor)
                return map.cells->cell(1, {123, 456});
        throw std::runtime_error("Tile actor missing from copied render scene");
    };
    check(cell(changed, target) == TileMapCell{2, 0} && !cell(frozen, target).tile,
          "Fixed-tick delta was lost or changed a retained copied frame");
    const auto old_world = candidate.at("ticket");
    auto cancelled = prepare();
    TileMapPlayReceiver staging;
    staging.observe(cancelled.at("tile_maps"));
    auto begin = staging.next_request();
    check(bool(begin), "Replacement candidate had no copied transfer");
    staging.receive(send(*begin).at("tile_projection"));
    send({{"command", "cancel"}, {"ticket", cancelled.at("ticket")}});
    auto stale = *staging.next_request();
    check(send(stale).at("tile_projection").value("stale", false),
          "Cancelled candidate served an old frozen tile page");
    check(send({{"command", "snapshot"}}).at("activation").at("generation") == old_world,
          "Cancelling tile replacement discarded the healthy active world");
    auto replacement = prepare();
    send({{"command", "snapshot"},
          {"candidate_ack",
           {{"session", session},
            {"ticket", replacement.at("ticket")},
            {"accepted", true},
            {"diagnostic", ""}}}});
    host.pump(now);
    active = send({{"command", "snapshot"}});
    receiver.observe(active.at("tile_maps"));
    check(!render(receiver).tile_maps[0].cells,
          "Replacement retained cells from the old active world");
    transfer(receiver);
    check(!cell(render(receiver), target).tile, "Replacement inherited a previous world's edit");
    check(send({{"command", "tile_map_begin"},
                {"world", old_world},
                {"scene", scene_id},
                {"entity", target},
                {"generation", before->generation},
                {"revision", 0u},
                {"base_generation", 0u},
                {"base_revision", 0u}})
              .at("tile_projection")
              .value("stale", false),
          "Superseded active world served copied map data");
    receiver.clear();
    check(!render(receiver).tile_maps[0].cells && cell(changed, target).tile == 2,
          "Stop retained current cells or invalidated an already copied frame");
}
