#pragma once
#include "../samples/native_sdk/tile_map_probe.hpp"
#include "runtime_tile_map.hpp"
#include <forge/native_sdk.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/sdk_client.hpp>
#include <future>

inline void test_sdk_tile_maps(const std::filesystem::path& root, const forge::Json& document,
                               const std::filesystem::path& library, forge::AssetId map_id,
                               forge::AssetId set_id) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    auto check = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    EngineContext engine(
        WorldRole::Runtime, false,
        {runtime_resources_module(root), load_native_sdk(library, "project.tile_map_probe", "1")});
    Scene scene(engine.world());
    scene.reset(document);
    const auto target = scene.reference(document.at("entities")[0].at("id").get<std::string>());
    const auto other = scene.reference(document.at("entities")[1].at("id").get<std::string>());
    const auto scene_id = target.scene.str(), entity_id = target.entity.str();
    auto& world = engine.world().world();
    const auto state_entity = world.lookup("tile_probe.state").id();
    const auto state_type = world.lookup("tile_probe.State").id();
    auto probe = [&]() -> TileMapProbe& {
        return *static_cast<TileMapProbe*>(ecs_get_mut_id(world, state_entity, state_type));
    };
    const auto* host = probe().host;
    sdk::Client client(host);
    check(client.callable(sdk::Capability::TileMaps) &&
              client.available(sdk::Capability::TileMapEdits) &&
              !client.callable(sdk::Capability::TileMapEdits) &&
              !(client.query(sdk::Capability::TileMaps).flags & FORGE_SDK_FIXED_ONLY) &&
              (client.query(sdk::Capability::TileMapEdits).flags & FORGE_SDK_FIXED_ONLY),
          "Tile capability availability/phase flags differ");
    const auto unsupported = client.query(sdk::Capability::TileMaps, 2);
    check(unsupported.version == 1 && !unsupported.available && !unsupported.callable,
          "Unsupported Tile Map capability version accepted");
    ForgeSdkTileMapV1 info{};
    check(client.tile_map(scene_id.c_str(), entity_id.c_str(), info) == 1 && !info.ready &&
              info.generation == 0 && std::string(info.requested_uuid) == map_id.str(),
          "Unsynchronized map status initialized state or claimed ready");
    ForgeSdkTileLayerV1 layer{};
    check(client.tile_layer(scene_id.c_str(), entity_id.c_str(), 1, 0, layer) == 0 && !layer.key,
          "Unready map exposed a layer");
    RuntimeSimulation simulation(engine.world(), scene);
    const auto end = std::chrono::steady_clock::now() + 15s;
    do {
        (void)simulation.presentation(0);
        check(client.tile_map(scene_id.c_str(), entity_id.c_str(), info) == 1,
              "Map status query failed during readiness");
        check(std::chrono::steady_clock::now() < end, "SDK Tile Map never became ready");
        if (!info.current)
            std::this_thread::sleep_for(1ms);
    } while (!info.current);
    check(info.ready && !info.edited && info.version == 1 && info.layer_count == 1 &&
              info.cell_size[0] > 0 && info.cell_size[1] > 0 &&
              std::string(info.adopted_uuid) == map_id.str() &&
              std::string(info.tile_set_uuid) == set_id.str() &&
              std::strlen(info.adopted_revision) == 64 &&
              std::string(info.requested_revision) == info.adopted_revision,
          "Copied map provenance or grid differs");
    check(client.tile_layer(scene_id.c_str(), entity_id.c_str(), info.generation, 0, layer) == 1 &&
              layer.key == 1 && layer.visible && layer.name[0],
          "SDK layer enumeration differs");
    const auto generation = info.generation;
    const std::array<ForgeSdkTileLocationV1, 3> locations{
        {{1, -1, -1}, {1, INT32_MIN, INT32_MAX}, {1, 0, 0}}};
    std::array<ForgeSdkTileCellV1, 3> cells{};
    auto read = [&] {
        return client.tile_cells(scene_id.c_str(), entity_id.c_str(), generation, locations.data(),
                                 cells.data(), cells.size());
    };
    check(read() == 1 && cells[0].tile == 2 && cells[1].tile == 0 && cells[2].tile == 0,
          "SDK negative/limit/empty cell lookup differs");
    auto invalid = locations;
    invalid[2].layer = UINT64_MAX;
    check(client.tile_cells(scene_id.c_str(), entity_id.c_str(), generation, invalid.data(),
                            cells.data(), cells.size()) == -1 &&
              !cells[0].tile && !cells[1].tile && !cells[2].tile,
          "Invalid bulk query partially copied results");
    check(client.tile_layer(scene_id.c_str(), entity_id.c_str(), generation, 1, layer) == -1 &&
              !layer.key &&
              client.tile_layer(scene_id.c_str(), entity_id.c_str(), generation + 1, 0, layer) ==
                  -2 &&
              !layer.key,
          "Invalid index/stale generation was accepted");
    info.size = sizeof(info) - 1;
    info.generation = 17;
    check(host->tile_map_inspect(host->context, scene_id.c_str(), entity_id.c_str(), &info) == -1 &&
              info.generation == 17,
          "Invalid-size output was written");
    check(client.tile_map("invalid", entity_id.c_str(), info) == -1 && !info.ready,
          "Malformed map reference accepted");
    std::vector<ForgeSdkTileLocationV1> full(FORGE_SDK_TILE_BATCH_LIMIT, locations[0]);
    std::vector<ForgeSdkTileCellV1> full_out(full.size());
    check(client.tile_cells(scene_id.c_str(), entity_id.c_str(), generation, full.data(),
                            full_out.data(), full.size()) == 1 &&
              full_out.back().tile == 2,
          "Maximum query batch rejected");
    full_out[0].tile = 123;
    check(host->tile_map_cells(host->context, scene_id.c_str(), entity_id.c_str(), generation,
                               full.data(), full_out.data(),
                               FORGE_SDK_TILE_BATCH_LIMIT + 1) == -1 &&
              full_out[0].tile == 123,
          "Invalid count wrote caller memory");
    ForgeSdkTileWriteV1 write{{1, 0, 0}, {3, 2, 0}};
    check(!client.write_tiles(scene_id.c_str(), entity_id.c_str(), generation, &write, 1) &&
              !host->tile_map_write(host->context, scene_id.c_str(), entity_id.c_str(), generation,
                                    &write, 1) &&
              !client.reset_tiles(scene_id.c_str(), entity_id.c_str(), generation),
          "Paused code submitted a cell command");
    check(std::async(std::launch::async,
                     [&] {
                         ForgeSdkTileMapV1 value{};
                         value.size = sizeof(value);
                         return host->tile_map_inspect(host->context, scene_id.c_str(),
                                                       entity_id.c_str(), &value) == -1 &&
                                !value.ready &&
                                !client.reset_tiles(scene_id.c_str(), entity_id.c_str(),
                                                    generation);
                     })
              .get(),
          "Tile SDK allowed a foreign thread");
    std::snprintf(probe().scene, sizeof(probe().scene), "%s", scene_id.c_str());
    std::snprintf(probe().entity, sizeof(probe().entity), "%s", entity_id.c_str());
    probe().tile = 3;
    probe().stage = 1;
    simulation.tick(1.f / 60);
    check(probe().stage == 2,
          probe().stage == 99 ? probe().error : "Module fixed system did not submit");
    (void)simulation.presentation(0);
    check(read() == 1 && !cells[2].tile, "Paused presentation published module edits");
    simulation.tick(1.f / 60);
    check(probe().stage == 3,
          probe().stage == 99 ? probe().error : "Module fixed system did not observe");
    check(read() == 1 && cells[2].tile == 3 && cells[2].flags == 3 &&
              runtime_tile_map_snapshot(engine.world(), other)->cells.cell(1, {}).tile == 0 &&
              scene.document() == document,
          "Module edits leaked into another instance or authored Scene");
    std::function<void()> fixed;
    world.system("test.TileCommands")
        .kind(host->fixed_phase)
        .run([&](flecs::iter&) {
            if (fixed)
                fixed();
        })
        .add(host->fixed_tag);
    auto in_fixed = [&](std::function<void()> action) {
        fixed = std::move(action);
        simulation.tick(1.f / 60);
        fixed = {};
    };
    in_fixed([&] {
        for (auto bad :
             {ForgeSdkTileWriteV1{{1, 0, 0}, {3, 8, 0}}, ForgeSdkTileWriteV1{{1, 0, 0}, {3, 0, 1}},
              ForgeSdkTileWriteV1{{1, 0, 0}, {UINT64_MAX, 0, 0}},
              ForgeSdkTileWriteV1{{UINT64_MAX, 0, 0}, {3, 0, 0}}}) {
            const std::array batch{write, bad};
            check(!client.write_tiles(scene_id.c_str(), entity_id.c_str(), generation, batch.data(),
                                      batch.size()),
                  "Invalid batch queued partially");
        }
        check(!client.write_tiles(scene_id.c_str(), entity_id.c_str(), generation + 1, &write, 1) &&
                  !client.write_tiles(scene_id.c_str(), entity_id.c_str(), generation, &write, 0) &&
                  !client.write_tiles(scene_id.c_str(), entity_id.c_str(), generation, nullptr, 1),
              "Stale/empty/null cell batch accepted");
    });
    uint64_t token = 0;
    in_fixed([&] {
        token = client.write_tiles(scene_id.c_str(), entity_id.c_str(), generation, &write, 1);
    });
    ForgeSdkTileEditV1 edit{};
    check(token && client.tile_edit(token, edit) == 1 &&
              edit.state == FORGE_SDK_TILE_EDIT_PENDING && client.release_tile_edit(token) &&
              !client.release_tile_edit(token),
          "Pending cell cancellation failed");
    simulation.tick(1.f / 60);
    check(read() == 1 && cells[2].flags == 3, "Cancelled command applied");
    const auto foreign =
        request_runtime_tile_cells(engine.world(), "project.foreign", target, generation,
                                   std::array{TileMapCellEdit{1, {}, {3, 0}}});
    check(client.tile_edit(foreign, edit) == 0 && !edit.state && !client.release_tile_edit(foreign),
          "Module accessed another owner's edit");
    release_runtime_tile_edit(engine.world(), "project.foreign", foreign);
    in_fixed([&] { token = client.reset_tiles(scene_id.c_str(), entity_id.c_str(), generation); });
    check(token != 0, "Fixed reset rejected");
    (void)simulation.presentation(0);
    check(read() == 1 && cells[2].tile == 3, "Paused reset applied");
    simulation.tick(1.f / 60);
    check(client.tile_edit(token, edit) == 1 && edit.state == FORGE_SDK_TILE_EDIT_APPLIED &&
              edit.generation != generation && edit.revision == 0 &&
              client.release_tile_edit(token) && read() == -2 && !cells[2].tile,
          "Reset failed to replace generation or stale query output");
    const auto current = edit.generation;
    std::vector<uint64_t> tokens;
    in_fixed([&] {
        for (unsigned i = 0; i < 64; ++i) {
            auto value =
                client.write_tiles(scene_id.c_str(), entity_id.c_str(), current, &write, 1);
            check(value != 0, "Module edit quota rejected early");
            tokens.push_back(value);
        }
        check(!client.write_tiles(scene_id.c_str(), entity_id.c_str(), current, &write, 1),
              "Module edit quota not enforced");
    });
    for (unsigned i = 0; i < 63; ++i)
        check(client.release_tile_edit(tokens[i]), "Could not release pending quota fixture");
    const auto before_stop = runtime_tile_map_snapshot(engine.world(), target);
    engine.world().modules().stop();
    check(!client.available(sdk::Capability::TileMaps) &&
              client.tile_edit(tokens.back(), edit) == 0 &&
              !host->tile_edit_release(host->context, tokens.back()),
          "Stopped module retained map access");
    publish_runtime_tile_edits(engine.world());
    bool cancelled = false;
    try {
        (void)inspect_runtime_tile_edit(engine.world(), "project.tile_map_probe", tokens.back());
    } catch (const std::exception&) {
        cancelled = true;
    }
    check(cancelled && before_stop->cells.cell(1, {}).tile == 0,
          "Stopped module's pending command applied");
}
