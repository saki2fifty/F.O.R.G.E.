#pragma once
#include "../src/runtime_entity_creation.hpp"
#include <forge/render_components.hpp>
#include <forge/scene.hpp>
#include <forge/sdk_client.hpp>
#include <forge/world.hpp>
#include <future>
#include <vector>
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
    {
        Scene primary(world);
        const auto document =
            migrate_scene({{"version", 1},
                           {"entities", Json::array({{{"id", "lookup"},
                                                      {"name", "Lookup target"},
                                                      {"components", Json::object()}}})}});
        primary.restore_snapshot(document);
        const auto handle = primary.entity("lookup").id();
        ForgeSdkEntityRefV1 reference{};
        check(client.entity_reference(handle, reference) == 1 &&
                  AssetId::parse(reference.scene_uuid) == primary.asset_id(),
              "Paused-boundary registered identity unavailable");
        {
            Scene duplicate(world);
            duplicate.restore_snapshot(document);
            check(client.entity_reference(handle, reference) == 0 && !reference.scene_uuid[0] &&
                      !reference.entity_uuid[0],
                  "Ambiguous loaded scene produced canonical identity");
        }
        check(client.entity_reference(handle, reference) == 1,
              "Unloading duplicate did not restore canonical identity");
    }
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

inline void test_sdk_prefabs(const ForgeSdkWorldV1* host, forge::WorldContext& world) {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    sdk::Client client(host);
    const auto prefab_id = AssetId::generate();
    const auto member_id = PrefabMemberId::generate();
    check(!client.request_prefab(nullptr, "Null") &&
              !client.request_prefab("invalid", "Bad identity") &&
              !client.request_prefab(prefab_id.str().c_str(), "") &&
              !client.request_prefab(prefab_id.str().c_str(), "Valid", "invalid") &&
              !host->prefab_request(nullptr, prefab_id.str().c_str(), nullptr, "Null context"),
          "SDK accepted malformed Prefab requests");
    for (unsigned source_version : {1u, 2u, 3u}) {
        Scene spawning(world);
        Json source{
            {"format", "forge.prefab"},
            {"version", source_version},
            {"asset_id", prefab_id},
            {"revision", 1u},
            {"root", member_id},
            {"members",
             Json::array({{{"id", member_id},
                           {"name", "Source"},
                           {"components",
                            {{"forge.local_translation", {{"x", 3}, {"y", 0}, {"z", 0}}}}}}})}};
        const auto schema = spawning.schema();
        bool health_admitted = false;
        for (const auto& type : schema.at("components"))
            if (type.at("id") == "project.health") {
                auto health = type.at("defaults");
                health["$forge"] = type.at("admission");
                health["health"] = 17.0;
                source["members"][0]["components"]["project.health"] = std::move(health);
                health_admitted = true;
            }
        check(health_admitted, "SDK fixture did not admit its actual C++ component");
        try {
            spawning.set_prefab_sources({{prefab_id, source}});
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("SDK custom source preparation: ") + error.what());
        }
        const auto created = client.request_prefab(prefab_id.str().c_str(), "SDK instance");
        ForgeSdkEntityV1 out{};
        check(created && client.inspect_entity(created, out) &&
                  out.state == FORGE_SDK_ENTITY_PENDING && !out.native_entity,
              "SDK Prefab did not enter pending state");
        const auto reserved = EntityId::parse(out.entity_uuid);
        const auto cancelled = client.request_prefab(prefab_id.str().c_str(), "Cancelled");
        check(client.release_entity(cancelled), "SDK could not cancel Prefab");
        const auto unavailable = client.request_prefab(AssetId::generate().str().c_str(), "Absent");
        detail::publish_runtime_entities(world);
        check(client.inspect_entity(created, out) && out.state == FORGE_SDK_ENTITY_READY &&
                  EntityId::parse(out.entity_uuid) == reserved &&
                  AssetId::parse(out.scene_uuid) == spawning.asset_id() &&
                  spawning.entity_count() == 1 && !spawning.can_undo(),
              "SDK Prefab publication did not preserve its reserved root identity");
        const auto entity = world.world().entity(out.native_entity);
        check(entity.get<LocalTranslation>().x == 3 && !entity.owns<LocalTranslation>(),
              "SDK Prefab source transform was flattened or lost");
        const auto health_type = world.world().lookup("sdk.AuthoredHealth").id();
        check(health_type && entity.has(health_type) && !entity.owns(health_type),
              "Spawned instance lost the actual DLL-owned custom component inheritance");
        auto health_value = [&](const std::string& id, const char* field = "health") {
            Json effective;
            try {
                effective = spawning.effective_document();
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string("SDK custom native projection: ") +
                                         error.what());
            }
            for (const auto& row : effective.at("entities"))
                if (row.at("id") == id)
                    return row.at("components").at("project.health").at(field).get<double>();
            throw std::runtime_error("Missing SDK custom value");
        };
        check(health_value(reserved.str()) == 17, "Custom Prefab data did not reach native codec");
        try {
            spawning.tune_runtime_property(reserved.str(), "project.health", "health", 19.0);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("SDK custom live edit: ") + error.what());
        }
        const auto other = client.request_prefab(prefab_id.str().c_str(), "Independent SDK data");
        detail::publish_runtime_entities(world);
        ForgeSdkEntityV1 copy{};
        check(client.inspect_entity(other, copy) && copy.state == FORGE_SDK_ENTITY_READY &&
                  health_value(copy.entity_uuid) == 17 && health_value(reserved.str()) == 19,
              "Custom component edit changed the shared Prefab or another instance");
        auto partial = spawning.document();
        for (auto& row : partial["entities"])
            if (row.at("id") == reserved.str()) {
                row["components"].erase("project.health");
                row["property_overrides"]["project.health"] = {
                    {"$forge",
                     source.at("members")[0].at("components").at("project.health").at("$forge")},
                    {"health", 19.0}};
                row["property_overrides"]["forge.mesh_renderer"] = {{"visible", false}};
            }
        spawning.replace(partial);
        spawning.tune_runtime_property(reserved.str(), "project.health", "lives", std::uint64_t(5));
        auto renderer = entity.get<MeshRenderer>();
        renderer.enabled = false;
        world.world().entity(entity.id()).set<MeshRenderer>(renderer);
        const auto append = client.request_entity("Unrelated append");
        detail::publish_runtime_entities(world);
        ForgeSdkEntityV1 added{};
        check(client.inspect_entity(append, added) && added.state == FORGE_SDK_ENTITY_READY &&
                  health_value(reserved.str(), "lives") == 5 &&
                  !entity.get<MeshRenderer>().enabled && !entity.get<MeshRenderer>().visible,
              "Runtime creation reset an unrelated C++ value outside the authored property mask");
        world.world().entity(added.native_entity).destruct();
        check(client.release_entity(append), "Could not release unrelated append observation");
        world.world().entity(copy.native_entity).destruct();
        check(client.inspect_entity(other, copy) && copy.state == FORGE_SDK_ENTITY_GONE &&
                  spawning.entity_count() == 1 && client.release_entity(other),
              "Native C++ destruction did not retire its root observation");
        ForgeSdkEntityV1 failure{};
        check(client.inspect_entity(unavailable, failure) &&
                  failure.state == FORGE_SDK_ENTITY_FAILED && failure.diagnostic[0] &&
                  client.release_entity(unavailable),
              "SDK unavailable Prefab did not report failed observation");
        check(std::async(
                  std::launch::async,
                  [&] { return !client.request_prefab(prefab_id.str().c_str(), "Wrong thread"); })
                  .get(),
              "SDK Prefab callback accepted a foreign thread");
        check(client.release_entity(created) && entity.is_alive(),
              "SDK observation release destroyed instance");
        std::vector<std::uint64_t> pending;
        for (unsigned i = 0; i < 64; ++i) {
            const auto token = i % 2 ? client.request_entity("Empty")
                                     : client.request_prefab(prefab_id.str().c_str(), "Prefab");
            check(token != 0, "Mixed entity/Prefab request bound rejected too early");
            pending.push_back(token);
        }
        check(!client.request_entity("Excess") &&
                  !client.request_prefab(prefab_id.str().c_str(), "Excess"),
              "Entity and Prefab observations did not share the module bound");
        for (auto token : pending)
            check(client.release_entity(token), "Could not cancel bounded mixed batch");
        detail::publish_runtime_entities(world);
        check(spawning.entity_count() == 1, "Cancelled mixed batch published members");
        entity.destruct();
        check(spawning.snapshot().at("entities").empty() &&
                  spawning.effective_document().at("entities").empty(),
              "Destroyed custom instance retained invalid projection intent");
        check(spawning.prefab_sources().at(prefab_id) == source,
              "Runtime scalar edits changed the original Prefab version or definition");
    }
}
