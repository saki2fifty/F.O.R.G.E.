#include "model_placement_tests.hpp"
#include "model_placement.hpp"
#include <forge/authoring.hpp>
#include <forge/geometry.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_components.hpp>
void test_model_placement(const forge::asset_detail::ModelSelection& selected,
                          const forge::AssetCatalog& catalog) {
    using namespace forge;
    using namespace forge::asset_detail;
    auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto rejects = [&](auto fn) {
        bool failed = false;
        try {
            fn();
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed, "Invalid model placement was accepted");
    };
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(empty_scene());
    const auto initial = scene.document();
    ModelPlacementOptions options;
    options.name = "Signed model";
    options.transform.translation = {10, 2, -3};
    options.transform.scale = {-2, 0, 1};
    const auto candidate =
        prepare_model_placement(selected, scene.asset_id(), scene.revision(), options);
    require(scene.document() == initial && !scene.can_undo(),
            "Preparing placement changed the scene");
    const auto root = instantiate_model(scene, catalog, candidate);
    const auto placed = scene.document();
    engine.world().evaluate_world_transforms();
    require(root == candidate.root && scene.entity_count() == 15 &&
                scene.entity(root.str()).get<ModelSource>().model.id == selected.owner &&
                !scene.entity(root.str()).get<ModelSource>().node.id,
            "Model placement did not create the ordinary wrapper and source hierarchy");
    std::map<AssetId, EntityId> entities;
    for (const auto& row : placed.at("entities")) {
        const auto entity = scene.entity(row.at("id"));
        require(primitive_kind(row) == no_primitive,
                "Placed model node incorrectly falls back to a legacy blockout cube");
        require(entity.owns<LocalTranslation>() && entity.owns<LocalRotation>() &&
                    entity.owns<LocalScale>() && entity.owns<WorldTransform>(),
                "Model placement lost independent TRS or derived world storage");
        require(!row.at("components").contains("forge.world_transform"),
                "Model placement serialized derived WorldTransform");
        const auto& source = entity.get<ModelSource>();
        if (!source.node.id)
            continue;
        require(entities.emplace(source.node.id, row.at("id").get<EntityId>()).second,
                "Model instance repeated source-node identity");
        const auto& member = selected.member(source.node.id);
        const auto& node = selected.index.hierarchy.at("nodes").at(*member.node);
        const auto scale = entity.get<LocalScale>();
        const auto& wanted = node.at("trs").at("scale");
        require(scale == checked_local_scale({wanted[0], wanted[1], wanted[2]}),
                "Source signed scale changed during scene placement");
        if (!node.at("mesh").is_null())
            require(entity.get<MeshRenderer>().mesh.id == selected.bindings.at(node.at("mesh")),
                    "Placed mesh does not use its logical asset identity");
    }
    scene.undo();
    require(scene.document() == initial, "Model placement undo did not remove the entire subtree");
    scene.redo();
    require(scene.document() == placed, "Model placement redo changed node identities");
    rejects([&] { instantiate_model(scene, catalog, candidate); });
    require(scene.document() == placed, "Stale placement changed the authored scene");
    auto duplicate = scene.duplicate_subtree(root.str());
    require(scene.entity_count() == 30 && duplicate != root.str() &&
                scene.entity(duplicate).get<ModelSource>().model.id == selected.owner,
            "Model subtree duplication lost source provenance or copied entity identity");
    scene.undo();
    auto entire_copy = duplicate_scene_asset(scene.document());
    require(entire_copy.at("asset_id").get<AssetId>() != scene.asset_id(),
            "Scene duplication retained asset identity");
    std::set<EntityId> old_ids;
    for (const auto& row : placed.at("entities"))
        old_ids.insert(row.at("id").get<EntityId>());
    for (const auto& row : entire_copy.at("entities")) {
        require(!old_ids.contains(row.at("id").get<EntityId>()),
                "Scene copy reused model entity UUID");
        require(row.at("components").at("forge.model_source").at("model").get<AssetId>() ==
                    selected.owner,
                "Scene copy rewrote imported model asset provenance");
    }
    const auto prefab = create_prefab_source(scene, root.str());
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    require(scene.entity(instance).get<ModelSource>().model.id == selected.owner &&
                !scene.entity(instance).owns<ModelSource>(),
            "Placed model provenance did not inherit through a structured prefab");
    const auto stale_source = prepare_model_placement(selected, scene.asset_id(), scene.revision());
    auto changed_catalog = catalog;
    auto record = changed_catalog.records().at(selected.owner);
    record.metadata["forge.import"]["generation"] = selected.generation + 1;
    changed_catalog.replace(record);
    const auto before_failure = scene.document();
    auto inconsistent_catalog = catalog;
    auto member_record = inconsistent_catalog.records().at(entities.begin()->first);
    member_record.metadata["forge.import"]["generation"] = selected.generation + 1;
    inconsistent_catalog.replace(member_record);
    rejects([&] { instantiate_model(scene, inconsistent_catalog, stale_source); });
    require(scene.document() == before_failure, "Inconsistent model member changed scene");
    member_record = catalog.records().at(entities.begin()->first);
    member_record.subasset->removed = true;
    inconsistent_catalog.replace(member_record);
    rejects([&] { instantiate_model(scene, inconsistent_catalog, stale_source); });
    require(scene.document() == before_failure, "Removed model member changed scene");
    rejects([&] { instantiate_model(scene, changed_catalog, stale_source); });
    require(scene.document() == before_failure, "Stale model revision changed scene/history");
    options.source_scene = UINT32_MAX;
    rejects(
        [&] { prepare_model_placement(selected, scene.asset_id(), scene.revision(), options); });
    auto tiny = selected;
    tiny.index.hierarchy["nodes"][0]["trs"]["scale"][0] = 1e-100;
    rejects([&] { prepare_model_placement(tiny, scene.asset_id(), scene.revision()); });
    tiny.index.hierarchy["nodes"][0]["trs"]["scale"][0] = -1e-100;
    rejects([&] { prepare_model_placement(tiny, scene.asset_id(), scene.revision()); });
    tiny.index.hierarchy["nodes"][0]["trs"]["scale"][0] = 0;
    const auto collapsed = prepare_model_placement(tiny, scene.asset_id(), scene.revision());
    require(!collapsed.entities.empty(), "Explicit zero-scale source could not be placed");
}

void test_model_animation_placement(const forge::asset_detail::ModelSelection& selected,
                                    const forge::AssetCatalog& catalog) {
    using namespace forge;
    using namespace forge::asset_detail;
    auto check = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(empty_scene());
    auto rejects = [&](auto action) {
        const auto before = scene.document();
        const auto revision = scene.revision();
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed && scene.document() == before && scene.revision() == revision,
              "Rejected animated placement changed scene or history");
    };
    const auto& animation = selected.index.hierarchy.at("animation");
    const auto skeleton = selected.bindings.at(animation.at("skeleton"));
    const auto clip = selected.bindings.at(animation.at("clips").at(0));
    const auto static_candidate =
        prepare_model_placement(selected, scene.asset_id(), scene.revision());
    const auto static_root = instantiate_model(scene, catalog, static_candidate);
    check(!scene.entity(static_root.str()).has<Animator>(),
          "Static skin placement invented a default autoplay animation");
    engine.world().evaluate_world_transforms();
    for (const auto& row : static_candidate.entities)
        check(scene.entity(row.at("id")).get<WorldTransform>().resolved,
              "Static skin placement left an unresolved source transform");
    scene.undo();
    check(scene.entity_count() == 0, "Static model placement undo left source nodes");
    ModelPlacementOptions options;
    options.name = "Animated instance";
    options.clip = {clip};
    const auto candidate =
        prepare_model_placement(selected, scene.asset_id(), scene.revision(), options);
    const auto root = instantiate_model(scene, catalog, candidate);
    const auto original = scene.document();
    const auto settings = scene.entity(root.str()).get<Animator>();
    check(settings.clip.id == clip && settings.skeleton.id == skeleton && settings.play_on_start &&
              settings.enabled,
          "Explicit model clip did not bind its own typed skeleton on the wrapper");
    scene.undo();
    check(scene.entity_count() == 0, "Animated placement undo was incomplete");
    scene.redo();
    check(scene.document() == original, "Animated placement redo changed source references/IDs");
    const auto duplicate = scene.duplicate_subtree(root.str());
    check(scene.entity(duplicate).get<Animator>() == settings && duplicate != root.str(),
          "Model duplication changed animation assets or reused the root identity");
    scene.undo();
    const auto prefab = create_prefab_source(scene, root.str());
    scene.publish_prefab_sources({{prefab.asset(), prefab.source}}, [] {});
    const auto instance = instantiate_prefab(scene, prefab.asset());
    check(scene.entity(instance).get<Animator>() == settings &&
              !scene.entity(instance).owns<Animator>(),
          "Model animation references failed native prefab inheritance");
    options.clip = {AssetId::generate()};
    rejects(
        [&] { prepare_model_placement(selected, scene.asset_id(), scene.revision(), options); });
    options.clip = {skeleton};
    rejects(
        [&] { prepare_model_placement(selected, scene.asset_id(), scene.revision(), options); });
    options.clip = {clip};
    auto prepared = prepare_model_placement(selected, scene.asset_id(), scene.revision(), options);
    auto corrupt = prepared;
    corrupt.entities[0]["components"]["forge.animator"]["clip"] = skeleton;
    rejects([&] { instantiate_model(scene, catalog, corrupt); });
    auto stale = catalog;
    auto record = stale.records().at(clip);
    record.metadata["forge.import"]["generation"] = selected.generation + 1;
    stale.replace(record);
    rejects([&] { instantiate_model(scene, stale, prepared); });
    stale = catalog;
    record = stale.records().at(skeleton);
    record.subasset->removed = true;
    stale.replace(record);
    rejects([&] { instantiate_model(scene, stale, prepared); });
    stale = catalog;
    record = stale.records().at(clip);
    record.dependency_edges.front().target = AssetId::generate();
    record.dependencies = {record.dependency_edges.front().target};
    stale.replace(record);
    rejects([&] { instantiate_model(scene, stale, prepared); });
}
