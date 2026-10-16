#pragma once
// Included after the native model Fixture: real publication, Ozz sampling and clocks.
void exit_source_tests(const AnimationStatesSource& original,
                       const std::shared_ptr<ProjectLease>& lease,
                       const std::filesystem::path& root) {
    auto source = original;
    source.document["states"][0]["exit"] = {{"state", 2}, {"phase", .25}, {"extension", 7}};
    source.document["states"][1]["exit"] = {{"state", 1}, {"phase", 1}};
    auto decoded = decode_animation_states(source.cook(), source.asset());
    require(decoded.state(1).exit_state == 2 && decoded.state(1).exit_phase == .25f &&
                decoded.state(2).exit_state == 1,
            "Cook lost stable cyclic exit rules");
    // JSON parse events count keys and both container boundaries, not just values.
    const auto events = [](const auto& self, const Json& value) -> std::size_t {
        if (!value.is_structured())
            return 1;
        std::size_t count = 2 + (value.is_object() ? value.size() : 0);
        for (const auto& child : value)
            count += self(self, child);
        return count;
    };
    auto boundary = source;
    boundary.document["opaque"] = Json::array();
    const auto padding = 10000 - events(events, boundary.document);
    for (std::size_t i = 0; i < padding; ++i)
        boundary.document["opaque"].push_back(0);
    require(events(events, boundary.document) == 10000 &&
                decode_animation_states(boundary.cook(), boundary.asset()).state(1).exit_state == 2,
            "Cooked envelope narrowed the original source structural budget");
    boundary.document["opaque"].push_back(0);
    rejects([&] { (void)boundary.cook(); }, "Cook widened source structural budget");
    const std::array documents{AssetReferenceDocument{"Assets/Exit.states.json", source.document}};
    EngineContext reference_engine;
    const auto references = collect_asset_references(reference_engine.world().schema(), documents);
    require(references.references.size() == 4 && references.uninspected.size() == 1 &&
                references.uninspected.front().find("exit.extension") != std::string::npos,
            "Local exit IDs treated as asset references or opaque rule data lost");
    for (const auto& value : {Json(0), Json(1), Json(99), Json(1.5), Json("2")}) {
        auto bad = source;
        bad.document["states"][0]["exit"]["state"] = value;
        rejects([&] { bad.validate(); }, "Invalid exit target admitted");
    }
    for (const auto& value : {Json(0), Json(-.1), Json(1.001), Json(".5"), Json(nullptr)}) {
        auto bad = source;
        bad.document["states"][0]["exit"]["phase"] = value;
        rejects([&] { bad.validate(); }, "Invalid exit phase admitted");
    }
    auto legacy = source;
    legacy.document["version"] = 1;
    legacy.document["states"][0]["exit"] = "opaque legacy extension";
    require(decode_animation_states(legacy.cook(), legacy.asset()).state(1).exit_state == 0,
            "Legacy opaque exit reinterpreted");
    legacy.document["asset_id"] = AssetId::generate();
    atomic_write(root / "Assets/LegacyExit.states.json", legacy.document.dump(2));
    AnimationStatesDocument history(lease, "Assets/LegacyExit.states.json");
    const auto before = history.source().document;
    const auto revision = history.revision();
    rejects([&] { history.edit(revision, "Unsafe upgrade", [](Json& d) { d["version"] = 2; }); },
            "Opaque legacy exit was silently converted");
    require(history.source().document == before && history.revision() == revision,
            "Rejected upgrade changed history");
    history.edit(revision, "Preserve opaque extension", [](Json& d) {
        for (auto& row : d["states"])
            if (row.contains("exit")) {
                row["custom_exit"] = row["exit"];
                row.erase("exit");
            }
    });
    history.edit(history.revision(), "Enable exit", [](Json& d) {
        d["version"] = 2;
        d["states"][0]["exit"] = {{"state", 2}, {"phase", .25}};
    });
    const auto upgraded = history.source().document;
    history.undo();
    require(history.source().document.at("version") == 1, "Undo lost legacy version");
    history.redo();
    require(history.source().document == upgraded, "Redo changed exit identity");
    rejects(
        [&] {
            history.edit(history.revision(), "Delete referenced target",
                         [](Json& d) { d["states"].erase(d["states"].begin() + 1); });
        },
        "Referenced exit target deleted");
    require(history.source().document == upgraded, "Rejected deletion changed source");
    history.save();
    AnimationStatesDocument reopened(lease, "Assets/LegacyExit.states.json");
    require(reopened.source().document == upgraded &&
                reopened.source().document.at("states")[0].at("custom_exit") ==
                    "opaque legacy extension",
            "Save/reopen lost exit or opaque extension");
}

Json exit_runtime_tests(const std::filesystem::path& root,
                        const std::shared_ptr<ProjectLease>& lease, const Json& original_doc,
                        EntityId actor, const AnimationStatesSource& markers, bool prepare_only) {
    auto publish = [&](AnimationStatesSource source) {
        const auto graph = AssetId::generate();
        source.document["asset_id"] = graph;
        const auto path = std::filesystem::path("Assets") / (graph.str() + ".states.json");
        atomic_write(root / path, source.document.dump(2));
        AssetImportService service(lease, animation_states_import_registry(),
                                   {"linux", "none", "cpu"}, 1);
        service.submit(
            service.prepare(path, {}, graph),
            [](auto& c, const auto& p, const auto&) { prepare_animation_states_publication(c, p); },
            [](const auto&, const auto&) {});
        require(service.wait_idle(10s), "Exit publication timed out");
        const auto results = service.poll();
        require(results.size() == 1 && results.front().published,
                results.empty() ? "Missing exit result" : results.front().diagnostic);
        auto doc = original_doc;
        for (auto& row : doc["entities"])
            if (row.at("components").contains("forge.animator"))
                row["components"]["forge.animator"]["states"] = graph;
        return doc;
    };
    auto source = markers;
    source.document["states"][0]["exit"] = {{"state", 2}, {"phase", .25}};
    const auto doc = publish(source);
    const auto graph =
        doc.at("entities")[0].at("components").at("forge.animator").at("states").get<AssetId>();
    auto record = AssetCatalog::open_project(root).records().at(graph);
    record.metadata["forge.import"]["output_version"] = 1;
    ResourcePool<AnimationStatesAsset> mismatched;
    const auto ticket = request_animation_states(mismatched, root, record);
    require(!mismatched.wait(ticket, 10s) && ticket.inspect().state == ResourceState::Failed &&
                ticket.inspect().diagnostic.find("cooked version/selection mismatch") !=
                    std::string::npos,
            "Cooked version did not match its selected metadata");
    if (prepare_only)
        return doc;
    constexpr float dt = .0625f;
    Fixture runtime(root, doc, actor), manual(root, original_doc, actor);
    const auto reference =
        *runtime.engine.world().reference(runtime.scene.entity(actor.str()).id());
    // Independent boundary: ceil(normalized phase * clip seconds / fixed delta).
    const auto duration = runtime.pose().at("duration").get<double>();
    const auto gate = std::uint64_t(std::ceil(.25 * duration / dt));
    for (std::uint64_t tick = 1; tick <= gate; ++tick) {
        if (tick == gate) {
            Fixture recovered(root, runtime.scene.document(), actor);
            recovered.animation->restore(runtime.animation->checkpoint());
            recovered.animation->tick(dt, tick);
            require(recovered.pose().at("state") == 2 && recovered.pose().at("time") == 0,
                    "Recovery before gate changed trigger tick");
        }
        runtime.animation->tick(dt, tick);
        manual.animation->tick(dt, tick);
        require(runtime.pose().at("state") == (tick < gate ? 1 : 2),
                "Automatic exit selected on the wrong tick");
    }
    auto entity = runtime.scene.entity(actor.str());
    require(entity.get<Animator>().state == 2 && runtime.animation->playback(reference)->state == 2,
            "Automatic state not visible through typed C++ component/status");
    near(runtime.pose().at("time"), 0, "Incoming clock consumed residual time");
    near(runtime.pose().at("blend_weight"), 0, "Exit bypassed incoming fade");
    same_pose(runtime.pose().at("local"), manual.pose().at("local"),
              "Exit boundary changed outgoing Ozz pose");
    require(runtime.animation->events().size() == 1 && runtime.animation->events()[0].marker == 2 &&
                runtime.animation->events()[0].state == 1,
            "Exit lost the final source tick marker");
    Fixture at_gate(root, runtime.scene.document(), actor);
    at_gate.animation->restore(runtime.animation->checkpoint());
    manual.state(2);
    for (unsigned step = 1; step <= 4; ++step) {
        const auto tick = gate + step;
        runtime.animation->tick(dt, tick);
        manual.animation->tick(dt, tick);
        at_gate.animation->tick(dt, tick);
        same_pose(runtime.pose().at("local"), manual.pose().at("local"),
                  "Automatic fade differs from admitted manual Ozz fade");
        same_pose(runtime.pose().at("local"), at_gate.pose().at("local"),
                  "Exit recovery diverged during fade");
        if (step == 1)
            require(runtime.animation->events().size() == 1 &&
                        runtime.animation->events()[0].marker == 5 &&
                        runtime.animation->events()[0].state == 2,
                    "Target entry marker did not belong to the next advancing tick");
    }
    Fixture midfade(root, runtime.scene.document(), actor);
    midfade.animation->restore(runtime.animation->checkpoint());
    runtime.state(1);
    midfade.state(1);
    runtime.animation->tick(dt, gate + 5);
    midfade.animation->tick(dt, gate + 5);
    require(runtime.pose().at("state") == 1,
            "C++ could not return to the initial state after an automatic exit");
    same_pose(runtime.pose().at("local"), midfade.pose().at("local"),
              "Recovered interrupted automatic fade differs");
    require(!runtime.animation->checkpoint().at("entries")[0].at("states").at("frozen").empty(),
            "Interrupted automatic fade did not preserve the admitted frozen pose");
    auto old_checkpoint = runtime.animation->checkpoint();
    old_checkpoint["version"] = 1;
    for (auto& entry : old_checkpoint["entries"])
        if (entry.contains("states"))
            entry["states"].erase("requested_state");
    Fixture legacy_recovery(root, runtime.scene.document(), actor);
    legacy_recovery.animation->restore(old_checkpoint);
    same_pose(legacy_recovery.pose().at("local"), runtime.pose().at("local"),
              "Legacy private checkpoint1 no longer restores its original selection");
    EngineContext prefab_author;
    Scene prefab_scene(prefab_author.world());
    prefab_scene.restore_snapshot(doc);
    const auto prefab = create_prefab_source(prefab_scene, actor.str());
    prefab_scene.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto instance = instantiate_prefab(prefab_scene, prefab.asset());
    authoring_command(prefab_scene, "property.set",
                      {{"entity", instance},
                       {"component", "forge.animator"},
                       {"field", "playback_speed"},
                       {"value", 1}});
    const auto prefab_authored = prefab_scene.snapshot();
    Json authored_mask;
    for (const auto& row : prefab_authored.at("entities"))
        if (row.at("id") == instance)
            authored_mask = row.at("property_overrides").at("forge.animator");
    require(authored_mask.contains("playback_speed") && !authored_mask.contains("state"),
            "State fixture did not begin with the intended partial authored mask");
    Fixture inherited(root, prefab_authored, EntityId::parse(instance));
    inherited.animation->tick(.5f, 1);
    require(inherited.pose().at("state") == 2, "Partial prefab did not advance automatically");
    Fixture inherited_copy(root, inherited.scene.snapshot(), EntityId::parse(instance));
    inherited_copy.animation->restore(inherited.animation->checkpoint());
    same_pose(inherited_copy.pose().at("local"), inherited.pose().at("local"),
              "Partial prefab lost automatic state during recovery");
    const auto inherited_snapshot = inherited.scene.snapshot();
    auto runtime_mask = authored_mask;
    runtime_mask["state"] = 2;
    for (const auto& row : inherited_snapshot.at("entities"))
        if (row.at("id") == instance)
            // ADR023 retains real native mutations outside a mask in private recovery.
            // The separate authored Scene/Prefab below must remain unchanged.
            require(
                row.at("property_overrides").at("forge.animator") == runtime_mask,
                "Private recovery lost the actual state transition or changed unrelated intent");
    Fixture rejected_copy(root, inherited.scene.snapshot(), EntityId::parse(instance));
    const auto retained = rejected_copy.animation->checkpoint();
    const auto retained_pose = rejected_copy.pose();
    const auto retained_component = rejected_copy.scene.entity(instance).get<Animator>();
    auto bad_recovery = inherited.animation->checkpoint();
    for (auto& entry : bad_recovery["entries"])
        if (entry.at("entity").at("entity") == instance)
            entry["clip_revision"] = "invalid";
    rejects([&] { rejected_copy.animation->restore(bad_recovery); },
            "Invalid recovery target revision admitted");
    require(rejected_copy.animation->checkpoint() == retained &&
                rejected_copy.pose() == retained_pose &&
                rejected_copy.scene.entity(instance).get<Animator>() == retained_component,
            "Rejected prepared recovery mutated prior player/component/pose");
    inherited.animation->tick(dt, 2);
    inherited_copy.animation->tick(dt, 2);
    same_pose(inherited_copy.pose().at("local"), inherited.pose().at("local"),
              "Recovered partial prefab diverged on its next fixed tick");
    require(prefab_scene.snapshot() == prefab_authored,
            "Runtime exit changed the authored prefab override intent");
    auto paused_doc = doc;
    for (auto& row : paused_doc["entities"])
        if (row.at("components").contains("forge.animator"))
            row["components"]["forge.animator"]["play_on_start"] = false;
    Fixture held(root, paused_doc, actor);
    held.animation->tick(1, 1);
    require(held.pose().at("state") == 1 && held.pose().at("time") == 0,
            "Held player advanced automatically");
    require(held.animation->control(reference, AnimationControl::Play, 2), "Held Play rejected");
    held.animation->tick(.5f, 2);
    require(held.pose().at("state") == 2 && held.pose().at("playing") == true,
            "Automatic exit forgot user-started playback intent");
    require(held.animation->control(reference, AnimationControl::Pause, 3), "Pause rejected");
    const auto paused = held.animation->checkpoint();
    held.animation->tick(1, 3);
    require(held.animation->checkpoint() == paused, "Pause changed exit/fade clocks");
    Fixture stopped(root, doc, actor);
    auto stopped_actor = stopped.scene.entity(actor.str());
    auto request = stopped_actor.get<Animator>();
    request.playback_speed = 0;
    stopped_actor.set(request);
    stopped.animation->tick(10, 1);
    require(stopped.pose().at("state") == 1 && stopped.pose().at("time") == 0,
            "Zero speed triggered an exit");
    request.playback_speed = 1;
    request.enabled = false;
    stopped_actor.set(request);
    stopped.animation->tick(10, 2);
    require(stopped.animation->playback(reference)->state == 1 && stopped.pose().is_null(),
            "Disabled player triggered an exit or exposed a rendered pose");
    SkeletalAnimationPreview preview;
    preview.start(root, doc, reference);
    while (!preview.prepare())
        std::this_thread::sleep_for(1ms);
    preview.seek(.75);
    require(preview.pose().at("state") == 1, "Seek triggered an automatic exit");
    preview.tick(dt);
    require(preview.pose().at("state") == 2, "Advancing after seek missed exit");
    auto cyclic = source;
    cyclic.document["states"][0]["exit"]["phase"] = 1;
    cyclic.document["states"][1]["exit"] = {{"state", 1}, {"phase", .001}};
    cyclic.document["states"][1]["transition_seconds"] = 0;
    const auto cycle_doc = publish(cyclic);
    Fixture cycle(root, cycle_doc, actor);
    cycle.animation->tick(3, 1);
    require(cycle.pose().at("state") == 2 && cycle.pose().at("time") == 0 &&
                cycle.pose().at("blend_weight") == 1,
            "Loop wrap/large advance failed or traversed multiple rules");
    cycle.animation->tick(3, 2);
    require(cycle.pose().at("state") == 1 && cycle.pose().at("time") == 0,
            "Cyclic transition lost stable target identity");
    auto nonlooping = cyclic;
    nonlooping.document["states"][0]["loop"] = false;
    Fixture endpoint(root, publish(nonlooping), actor);
    endpoint.animation->tick(3, 1);
    require(endpoint.pose().at("state") == 2 && endpoint.pose().at("playing") == true &&
                endpoint.pose().at("time") == 0,
            "Nonlooping endpoint exit lost playback or consumed residual time");
    auto legacy_source = cyclic;
    legacy_source.document["version"] = 1;
    legacy_source.document["states"][0]["exit"] = "opaque extension";
    Fixture legacy_manual(root, publish(legacy_source), actor);
    legacy_manual.animation->tick(3, 1);
    require(legacy_manual.pose().at("state") == 1,
            "Published legacy source acquired automatic behavior");
    Fixture failure(root, cycle_doc, actor);
    unsigned validations = 0;
    failure.animation->pose_validator([&](const auto&) {
        if (++validations == 2)
            throw std::runtime_error("test exit candidate rejected by transform owner");
    });
    failure.animation->tick(3, 1);
    require(failure.pose().at("state") == 1 &&
                failure.scene.entity(actor.str()).get<Animator>().state == 0 &&
                !failure.animation->checkpoint_ready(),
            "Failed automatic candidate changed the admitted player/component");
    const auto after_failure = validations;
    failure.animation->tick(3, 2);
    require(validations == after_failure + 1 && failure.pose().at("state") == 1,
            "Unchanged rejected automatic candidate was retried every tick");
    failure.animation->pose_validator({});
    failure.state(2);
    failure.animation->tick(dt, 3);
    require(failure.pose().at("state") == 1 && failure.animation->checkpoint_ready(),
            "Manual selection could not clear a failed automatic candidate and advance");
    Fixture reload(root, cycle_doc, actor);
    unsigned reload_validations = 0;
    reload.animation->pose_validator([&](const auto&) {
        if (++reload_validations == 2)
            throw std::runtime_error("test rejected automatic target before refresh");
    });
    reload.animation->tick(3, 1);
    require(!reload.animation->checkpoint_ready(), "Rejected automatic target not retained");
    reload.animation->pose_validator({});
    reload.animation->catalog(
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    reload.animation->tick(3, 2);
    require(reload.pose().at("state") == 2 && reload.animation->checkpoint_ready(),
            "Explicit catalog refresh did not retry the rejected automatic target");
    return doc;
}
