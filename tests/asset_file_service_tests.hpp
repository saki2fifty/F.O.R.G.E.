#pragma once
#include "asset_file_service.hpp"
#include "asset_storage.hpp"
#include <forge/material_source.hpp>
#include <forge/scene.hpp>
inline void test_asset_file_service(const std::filesystem::path& root) {
    using namespace forge;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto schema = scene.schema();
    // Copied schema structure from native Meta supports nested vectors/structs and EntityRef.
    const Json element = {
        {"type", "struct"},
        {"fields", Json::array({{{"id", "asset"}, {"type", "asset_ref"}, {"asset_type", "scene"}},
                                {{"id", "entity"}, {"type", "entity_ref"}},
                                {{"id", "text"}, {"type", "string"}}})}};
    const Json sequence = {{"id", "targets"}, {"type", "vector"}, {"element", element}};
    schema["components"].push_back(
        {{"id", "test.references"}, {"fields", Json::array({sequence})}});
    const auto target = AssetId::generate(), consumer = AssetId::generate();
    const auto entity = EntityId::generate();
    auto source = empty_scene();
    source["asset_id"] = target;
    auto used = empty_scene();
    used["asset_id"] = consumer;
    used["entities"] =
        Json::array({{{"id", entity},
                      {"name", "Consumer"},
                      {"components",
                       {{"test.references",
                         {{"targets", Json::array({{{"asset", target},
                                                    {"entity", EntityRef{target, entity}},
                                                    {"text", target.str()},
                                                    {"unknown", target.str()}}})}}},
                        {"opaque.plugin", {{"looks_like_ref", target}}}}}}});
    auto native_refs = empty_scene();
    native_refs["entities"] = Json::array(
        {{{"id", EntityId::generate()},
          {"name", "Native Meta references"},
          {"components",
           {{"forge.mesh_renderer",
             {{"mesh", target},
              {"materials", Json::array({{{"slot", "surface"}, {"material", target}}})}}}}}}});
    const std::array inspected{AssetReferenceDocument{"native.scene.json", native_refs}};
    check(inspect_asset_references(scene.schema(), inspected, {target}).references.size() == 2,
          "Native MeshRenderer Meta did not expose mesh and nested material references");
    const auto collected = collect_asset_references(scene.schema(), inspected);
    check(collected.references.size() == 2 && collected.uninspected.empty(),
          "Complete reference collection lost unresolved identities");
    check(collected.references[0].expected_type == MeshAsset::type &&
              collected.references[1].expected_type == MaterialAsset::type,
          "Complete reference collection lost expected asset types");
    check(inspect_asset_references(scene.schema(), inspected, {}).references.empty(),
          "Empty impact target filter changed semantics");
    {
        auto lighting = empty_scene();
        lighting["rendering"] = {{"version", 1u}, {"environment", {{"texture", target}}}};
        const std::array docs{AssetReferenceDocument{"lighting.scene.json", lighting}};
        const auto refs = collect_asset_references(scene.schema(), docs);
        check(refs.references.size() == 1 && refs.references[0].target == target &&
                  refs.references[0].expected_type == TextureAsset::type &&
                  refs.uninspected.empty(),
              "Scene environment is absent from complete reference collection");
        lighting["rendering"]["environment"]["future_binding"] = target;
        const std::array unknown{AssetReferenceDocument{"future.scene.json", lighting}};
        check(!collect_asset_references(scene.schema(), unknown).uninspected.empty(),
              "Unknown rendering dependency coverage was silently accepted");
    }
    {
        // The same authored reference can occur in partial prefab intent. Only
        // metadata admitted for this exact envelope may interpret custom data.
        auto custom_schema = schema;
        auto& declaration = custom_schema["components"].back();
        const Json stamp = {{"format", "forge.authored-component"},
                            {"version", 1},
                            {"module", "test.module"},
                            {"schema_version", 1},
                            {"digest", "fixture"}};
        declaration["custom"] = true;
        declaration["admission"] = stamp;
        auto partial = used;
        auto& row = partial["entities"][0];
        row["property_overrides"]["test.references"] = row["components"]["test.references"];
        row["property_overrides"]["test.references"]["$forge"] = stamp;
        row["components"].erase("test.references");
        auto inspect = [&] {
            const std::array docs{AssetReferenceDocument{"partial.scene.json", partial}};
            return inspect_asset_references(custom_schema, docs, {target});
        };
        const auto found = inspect();
        check(found.references.size() == 2,
              "Partial custom prefab intent lost known AssetRef/EntityRef impact");
        check(
            std::none_of(found.uninspected.begin(), found.uninspected.end(),
                         [](const auto& path) { return path.find("$forge") != std::string::npos; }),
            "Admitted schema identity was reported as unknown payload");
        row["property_overrides"]["test.references"]["$forge"]["schema_version"] = 2;
        const auto mismatch = inspect();
        check(mismatch.references.empty() && !mismatch.uninspected.empty(),
              "Incompatible custom metadata was treated as known reference coverage");
    }
    asset_storage::replace(root / "Assets/target.scene.json", source.dump(2));
    asset_storage::replace(root / "Assets/consumer.scene.json", used.dump(2));
    auto material = MaterialSource::create(AssetId::generate());
    material.document["base"] = target;
    asset_storage::replace(root / "Assets/unpublished.material.json", material.document.dump(2));
    asset_storage::replace(
        root / "forge.project.json",
        Json{{"startup_scene", {{"asset", target}, {"source", "Assets/target.scene.json"}}}}.dump(
            2));
    asset_storage::replace(root / "arbitrary.json", "broken unrelated JSON");
    asset_storage::replace(root / "untyped.json",
                           Json{{"kind", 1}, {"entities", false}, {"opaque", target}}.dump(2));
    AssetRecord record{target, "scene", "Assets/target.scene.json", 3, {}};
    AssetFileService service(lease);
    const auto finish = [&] {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        do {
            service.poll();
            check(std::chrono::steady_clock::now() < deadline, "File service job stalled");
            std::this_thread::sleep_for(1ms);
        } while (service.state() == AssetFileState::Preparing ||
                 service.state() == AssetFileState::Committing);
    };
    service.prepare(record, {AssetFileAction::Delete, target, {}}, schema);
    finish();
    if (service.state() != AssetFileState::Review)
        throw std::runtime_error("File review preparation failed: " + service.diagnostic());
    check(service.review()->impact.references.size() == 4,
          "Known nested references were missed or opaque strings treated as references");
    check(service.review()->impact.uninspected.size() >= 2, "Opaque reference coverage was hidden");
    check(std::filesystem::exists(root / record.source),
          "Review mutated source before confirmation");
    check(AssetCatalog::open_project(root).records().contains(target),
          "Discovered authored identity was not safely indexed");
    service.cancel();
    check(service.state() == AssetFileState::Cancelled && !service.busy(),
          "Review cancellation did not release operation");
    service.prepare(record, {AssetFileAction::Delete, target, {}}, schema);
    finish();
    const auto before = *asset_storage::read(root / "forge.assets.json");
    used["entities"][0]["name"] = "Changed after review";
    asset_storage::replace(root / "Assets/consumer.scene.json", used.dump(2));
    service.commit();
    finish();
    check(service.state() == AssetFileState::Failed &&
              service.diagnostic().find("changed since review") != std::string::npos,
          "Changed reference document did not invalidate delete review");
    check(std::filesystem::exists(root / record.source) &&
              *asset_storage::read(root / "forge.assets.json") == before,
          "Stale delete changed source/catalog");
    service.prepare(record, {AssetFileAction::Delete, target, {}}, schema);
    finish();
    // A new source since review must invalidate even if previously inspected files are
    // unchanged.
    asset_storage::replace(root / "Assets/new.scene.json", empty_scene().dump(2));
    service.commit();
    finish();
    check(service.state() == AssetFileState::Failed, "New authored source bypassed impact review");
    service.prepare(record, {AssetFileAction::Delete, target, {}}, schema);
    finish();
    const auto original_consumer = *asset_storage::read(root / "Assets/consumer.scene.json");
    service.commit();
    finish();
    check(service.state() == AssetFileState::Complete && service.receipt() &&
              !service.receipt()->retained_files.empty(),
          "Reviewed delete failed to retain recoverable source files");
    check(!std::filesystem::exists(root / record.source) &&
              !service.catalog()->records().contains(target) &&
              *asset_storage::read(root / "Assets/consumer.scene.json") == original_consumer,
          "Delete retargeted/revised referencing source or retained selected identity");
    // Authoring thread ownership also applies to detached job orchestration.
    bool rejected = false;
    std::thread other([&] {
        try {
            service.poll();
        } catch (const std::exception&) {
            rejected = true;
        }
    });
    other.join();
    check(rejected, "File service admitted a foreign application thread");
}
