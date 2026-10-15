// Included inside the blend-space pipeline test's anonymous namespace.
void mix_three(const std::array<Json, 3>& poses, const Json& actual,
               const std::array<double, 3>& weights) {
    require(actual.at("local").size() == poses[0].at("local").size(), "2D pose lost joints");
    for (std::size_t joint = 0; joint < actual.at("local").size(); ++joint) {
        for (const auto* field : {"translation", "scale"})
            for (unsigned axis = 0; axis < 3; ++axis) {
                double expected = 0;
                for (unsigned layer = 0; layer < 3; ++layer)
                    expected +=
                        weights[layer] * poses[layer]["local"][joint][field][axis].get<double>();
                near(actual["local"][joint][field][axis], expected);
            }
        std::array<double, 4> rotation{};
        for (unsigned layer = 0; layer < 3; ++layer) {
            double dot = 0;
            for (unsigned axis = 0; axis < 4; ++axis)
                dot += poses[0]["local"][joint]["rotation"][axis].get<double>() *
                       poses[layer]["local"][joint]["rotation"][axis].get<double>();
            for (unsigned axis = 0; axis < 4; ++axis)
                rotation[axis] += weights[layer] * (dot < 0 ? -1 : 1) *
                                  poses[layer]["local"][joint]["rotation"][axis].get<double>();
        }
        double length = 0;
        for (const auto value : rotation)
            length += value * value;
        for (unsigned axis = 0; axis < 4; ++axis)
            near(actual["local"][joint]["rotation"][axis], rotation[axis] / std::sqrt(length));
    }
    require(!actual.at("morphs").empty(),
            "2D morph values/defaults missing at X=" + actual.at("blend_position").dump() +
                ", Y=" + actual.at("blend_position_y").dump());
    require(actual.at("morphs").size() == 1 && actual.at("morphs")[0].at("weights").size() == 2,
            "2D fixture changed its single two-target morph node");
    // OverlayColumn's authored defaults are .25/0. A clip with no morph curve
    // has an empty observation and contributes those defaults, not an array row.
    constexpr std::array<double, 2> defaults{.25, 0};
    for (std::size_t node = 0; node < actual.at("morphs").size(); ++node)
        for (std::size_t target = 0; target < actual["morphs"][node]["weights"].size(); ++target) {
            double expected = 0;
            for (unsigned layer = 0; layer < 3; ++layer) {
                const auto& morphs = poses[layer].at("morphs");
                if (morphs.empty())
                    expected += weights[layer] * defaults.at(target);
                else {
                    require(morphs.at(node).at("node") == actual.at("morphs").at(node).at("node"),
                            "2D morph node identity differs");
                    expected +=
                        weights[layer] * morphs.at(node).at("weights").at(target).get<double>();
                }
            }
            near(actual["morphs"][node]["weights"][target], expected);
        }
}
void plane_root_motion(const std::filesystem::path& project, Json source, EntityId actor) {
    for (auto& row : source["entities"])
        if (row.at("id") == Json(actor))
            row["components"]["forge.animator"]["root_motion"] = 2;
    World extracted(project, source, actor);
    extracted.tick(.5f);
    const auto delta = extracted.animation->root_motion(extracted.ref());
    require(delta && !delta->applied, "Three-layer root extraction absent");
    // Authored Base moves Z at1m/s, Overlay X at2m/s and Y at60deg/s;
    // their durations are2s/1s. The2s cycle scales Overlay's interval by1/2.
    near(delta->translation[0], .125);
    near(delta->translation[1], 0);
    near(delta->translation[2], .125);
    const auto pi = std::acos(-1.);
    near(delta->rotation[1], std::sin(pi / 96));
    near(delta->rotation[3], std::cos(pi / 96));
    for (unsigned axis = 0; axis < 3; ++axis)
        near(extracted.pose()["local"][0]["translation"][axis], 0);
    auto config = extracted.entity().get<Animator>();
    config.blend_position = config.blend_position_y = 0;
    extracted.entity().set(config);
    extracted.tick(.25f);
    const auto stationary = extracted.animation->root_motion(extracted.ref());
    require(bool(stationary), "Three-layer to point root observation absent");
    for (const auto coordinate : stationary->translation)
        near(coordinate, 0);
    World loops(project, source, actor);
    loops.tick(7);
    const auto loop = loops.animation->root_motion(loops.ref());
    require(bool(loop), "Three-layer whole-cycle extraction absent");
    // Overlay's three60deg cycles and half-cycle trace X=1,Z=-2sqrt(3).
    // Base contributes Z=7; each has1/4 influence and Rest has1/2.
    near(loop->translation[0], .25);
    near(loop->translation[2], 1.75 - std::sqrt(3.) * .5);
    near(loop->rotation[1], -std::sin(pi * 37.5 / 360));
    near(loop->rotation[3], std::cos(pi * 37.5 / 360));
    for (auto& row : source["entities"])
        if (row.at("id") == Json(actor))
            row["components"]["forge.animator"]["root_motion"] = 1;
    World applied(project, source, actor);
    applied.tick(.5f);
    const auto moved = applied.engine.world().get_local_transform(applied.entity());
    near(moved.translation.x, .125);
    near(moved.translation.z, .125);
    near(moved.rotation.y, std::sin(pi / 96));
    const auto checkpoint = applied.animation->checkpoint();
    World recovered(project, applied.scene.document(), actor, false);
    recovered.animation->restore(checkpoint);
    recovered.tick_id = applied.tick_id;
    const auto restored = recovered.engine.world().get_local_transform(recovered.entity());
    near(restored.translation.x, moved.translation.x);
    near(restored.translation.z, moved.translation.z);
    applied.tick(.25f);
    recovered.tick(.25f);
    const auto a = applied.engine.world().get_local_transform(applied.entity());
    const auto b = recovered.engine.world().get_local_transform(recovered.entity());
    near(a.translation.x, b.translation.x);
    near(a.translation.z, b.translation.z);
    near(a.rotation.y, b.rotation.y);
    // Actual native Jolt authority, not a callback-only veto.
    for (const auto motion : {1u, 2u}) {
        EngineContext engine(WorldRole::Runtime, false,
                             {physics_module(), animation_module(project)});
        Scene scene(engine.world());
        RuntimeSimulation simulation(engine.world(), scene);
        const auto animation = animation_runtime(engine.world());
        const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
        Json body, collider;
        for (const auto& type : detail::builtins()) {
            if (std::string_view(type.name) == "forge.physics_body")
                body = type.defaults;
            if (std::string_view(type.name) == "forge.box_collider")
                collider = type.defaults;
        }
        body["motion"] = motion;
        body["mass"] = 1;
        body["gravity_factor"] = 0;
        auto physical_source = source;
        for (auto& row : physical_source["entities"])
            if (row.at("id") == Json(actor)) {
                row["components"]["forge.physics_body"] = body;
                row["components"]["forge.box_collider"] = collider;
                if (motion == 2)
                    row["spatial"]["mode"] = "world";
            }
        scene.restore_snapshot(physical_source);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "2D physics pose admission timeout");
            std::this_thread::sleep_for(1ms);
        }
        physics->synchronize(1.f / 60);
        const auto entity = scene.entity(actor.str());
        const auto before = engine.world().get_local_transform(entity);
        std::string failure;
        try {
            simulation.tick(1.f / 60);
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto native = physics->body_state(scene.reference(actor.str()));
        require(native && native->motion == motion,
                "2D root fixture did not realize its Jolt body");
        const auto after = engine.world().get_local_transform(entity);
        if (motion == 1) {
            require(failure.empty() && animation->checkpoint_ready() &&
                        after.translation.x > before.translation.x &&
                        after.translation.z > before.translation.z,
                    "Three-layer root Apply did not reach a Kinematic body");
            require(std::abs(native->centre_of_mass[0] - after.translation.x) < 1e-4 &&
                        std::abs(native->centre_of_mass[2] - after.translation.z) < 1e-4,
                    "Three-layer Kinematic body and native actor disagree");
        } else {
            const auto diagnostics = Json(engine.services().diagnostics()).dump();
            require(!animation->checkpoint_ready() && after == before &&
                        (failure.find("Dynamic") != std::string::npos ||
                         diagnostics.find("Dynamic") != std::string::npos),
                    "Three-layer root motion bypassed Dynamic body authority");
        }
    }
}
template <class Selection>
void plane_rig(const std::filesystem::path& project, Json source, EntityId actor,
               const Selection& selected) {
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(source);
    const auto root = selected.bindings.at("/nodes/0");
    const auto middle = selected.bindings.at("/nodes/1");
    const auto end = selected.bindings.at("/nodes/4");
    auto reach = TwoBoneIK{};
    reach.start = {root};
    reach.middle = {middle};
    reach.end = {end};
    reach.enabled = true;
    scene.entity(actor.str()).set(reach);
    World live(project, scene.snapshot(), actor);
    live.tick(.5f);
    const auto pose = live.pose();
    const auto found =
        std::find(pose.at("joint_assets").begin(), pose.at("joint_assets").end(), Json(end));
    require(found != pose.at("joint_assets").end(), "Three-layer rig endpoint absent");
    const auto index = std::size_t(found - pose.at("joint_assets").begin());
    for (unsigned c = 0; c < 3; ++c)
        near(pose.at("model")[index][12 + c], std::array<double, 3>{1, 1, 0}[c]);
    written_pose(live);
    live.entity().remove<TwoBoneIK>();
    live.animation->synchronize();
    require(live.animation->prepare_initial_pose(), "Three-layer rig removal remained pending");
    World plain(project, source, actor);
    plain.tick(.5f);
    same(live.pose(), plain.pose());
    const auto held = live.pose();
    live.animation->pose_validator([](const auto&) { throw std::runtime_error("2D pose veto"); });
    auto config = live.entity().get<Animator>();
    config.blend_position = .75f;
    config.blend_position_y = .25f;
    live.entity().set(config);
    live.animation->synchronize();
    require(!live.animation->checkpoint_ready(), "Three-layer pose veto ignored");
    same(held, live.pose());
    written_pose(live);
    live.animation->pose_validator({});
    config.blend_position = config.blend_position_y = .5f;
    live.entity().set(config);
    live.animation->synchronize();
    require(live.animation->checkpoint_ready(), "Three-layer pose failed to recover from veto");
    config.blend_space.id = AssetId::generate();
    live.entity().set(config);
    live.animation->synchronize();
    require(!live.animation->checkpoint_ready(), "Unavailable desired 2D asset admitted");
    same(held, live.pose());
    written_pose(live);
}

void plane_budget(const std::filesystem::path& project, std::shared_ptr<ProjectLease> lease) {
    const auto catalog = AssetCatalog::open_project(project);
    AssetId model;
    for (const auto& [id, record] : catalog.records())
        if (record.type == ModelAsset::type && record.source == "Assets/QuaterniusLocomotion.glb")
            model = id;
    require(bool(model), "Existing large budget rig absent");
    const auto selected = load_model_selection(project, catalog, model);
    const auto skeleton = selected.bindings.at("/rig/skeleton");
    std::vector<AssetId> clips;
    for (const auto& [id, record] : catalog.records())
        if (record.type == AnimationClipAsset::type && record.subasset &&
            record.subasset->owner == model)
            clips.push_back(id);
    require(clips.size() >= 3, "Budget rig needs three existing clips");
    auto document = AnimationBlendSpaceDocument::create(
        lease, "Assets/BudgetPlane.blendspace.json", [&](AssetId id) {
            auto source = AnimationBlendSpaceSource::create(id, 2);
            source.document["skeleton"] = skeleton;
            const std::array<std::array<int, 2>, 3> points{{{0, 0}, {1, 0}, {0, 1}}};
            for (unsigned i = 0; i < 3; ++i)
                source.document["samples"].push_back(
                    {{"id", AssetId::generate()},
                     {"clip", clips[i]},
                     {"position", {{"x", points[i][0]}, {"y", points[i][1]}}}});
            return source;
        });
    AssetImportService imports(lease, animation_blend_space_import_registry(),
                               {"windows", "d3d12", "desktop"}, 1);
    imports.submit(
        imports.prepare(document->locator(), {}, document->source().asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_animation_blend_space_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    require(imports.wait_idle(15s), "Budget plane publication timeout");
    const auto receipts = imports.poll();
    require(receipts.size() == 1 && receipts[0].published, "Budget plane publication failed");
    EngineContext author;
    Scene scene(author.world());
    const auto first = authoring_command(scene, "entity.create", {{"name", "Budget probe"}})
                           .at("selected")
                           .get<EntityId>();
    Animator config;
    config.blend_space.id = document->source().asset();
    config.blend_position = config.blend_position_y = .25f;
    scene.entity(first.str()).set(config);
    World probe(project, scene.snapshot(), first);
    const auto tracks = probe.pose().at("local").size();
    const auto capacity = 8192 / tracks;
    const auto triples = (capacity - 2) / 3;
    const auto singles = (capacity - 2) % 3;
    require(triples > 0 && triples + singles + 2 <= 256, "Budget fixture cannot reach joint bound");
    auto source = scene.snapshot();
    source["entities"] = Json::array();
    auto settings = probe.scene.snapshot().at("entities")[0].at("components").at("forge.animator");
    std::vector<EntityId> actors;
    for (std::size_t i = 0; i < triples + singles; ++i) {
        const auto id = EntityId::generate();
        actors.push_back(id);
        auto value = settings;
        if (i >= triples)
            value["blend_position"] = value["blend_position_y"] = 0;
        source["entities"].push_back({{"id", id},
                                      {"name", "Budget player"},
                                      {"spatial", {{"mode", "world"}}},
                                      {"components", {{"forge.animator", value}}}});
    }
    const auto overflow = EntityId::generate();
    Json state_scene;
    std::ifstream(project / "Scenes/10-animation-states.scene.json") >> state_scene;
    Json state_settings;
    for (const auto& row : state_scene.at("entities"))
        if (row.at("components").contains("forge.animator")) {
            state_settings = row.at("components").at("forge.animator");
            break;
        }
    state_settings["state"] = 1;
    const auto state_actor = EntityId::generate();
    source["entities"].push_back({{"id", state_actor},
                                  {"name", "Mixed state player"},
                                  {"spatial", {{"mode", "world"}}},
                                  {"components", {{"forge.animator", state_settings}}}});
    source["entities"].push_back(
        {{"id", overflow}, {"name", "Overflow"}, {"components", Json::object()}});
    World live(project, source, actors[0]);
    const auto state_entity = live.scene.entity(state_actor.str());
    auto state_config = state_entity.get<Animator>();
    state_config.state = 2;
    state_entity.set(state_config);
    live.tick(.01f);
    require(live.animation->checkpoint_ready(),
            "Mixed three-layer/state budget failed within limit");
    const auto full = live.animation->checkpoint();
    auto point = config;
    point.blend_position = point.blend_position_y = 0;
    const auto extra = live.scene.entity(overflow.str());
    extra.set(point);
    live.tick(.01f);
    require(!live.animation->checkpoint_ready() &&
                live.animation->presentation(extra.id(), 1).is_null(),
            "Three-layer work was undercounted at admission");
    live.tick(1);
    // A failed unchanged desired configuration is retried after an explicit edit.
    extra.remove<Animator>();
    live.animation->synchronize();
    extra.set(point);
    live.tick(.01f);
    require(live.animation->checkpoint_ready() &&
                !live.animation->presentation(extra.id(), 1).is_null(),
            "Completing the mixed state fade did not free the shared joint budget");
    const auto admitted = live.animation->checkpoint();
    auto oversized = admitted;
    for (auto& entry : oversized["entries"])
        for (const auto& previous : full.at("entries"))
            if (entry.at("entity") == previous.at("entity"))
                entry = previous;
    bool rejected = false;
    try {
        live.animation->restore(oversized);
    } catch (const std::exception& error) {
        rejected =
            std::string_view(error.what()).find("evaluated-joint budget") != std::string_view::npos;
        if (!rejected)
            std::cerr << "Unexpected mixed budget recovery rejection: " << error.what() << '\n';
    }
    require(rejected && live.animation->checkpoint() == admitted,
            "Three-layer over-budget recovery mutated clocks or failed for another reason");
}
template <class Selection>
void plane_pipeline(const std::filesystem::path& project, AssetCatalog catalog,
                    const Selection& selected, AssetId skeleton, AssetId rest, AssetId base,
                    AssetId overlay) {
    auto lease = std::make_shared<ProjectLease>(project);
    auto document = AnimationBlendSpaceDocument::create(
        lease, "Assets/PlaneMotion.blendspace.json", [&](AssetId id) {
            auto source = AnimationBlendSpaceSource::create(id, 2);
            source.document["skeleton"] = skeleton;
            source.document["cycle_seconds"] = 2;
            const std::array<std::array<double, 2>, 4> points{{{0, 0}, {2, 0}, {0, 2}, {3, 3}}};
            const std::array clips{rest, base, overlay, rest};
            for (unsigned i = 0; i < 4; ++i)
                source.document["samples"].push_back(
                    {{"id", AssetId::generate()},
                     {"clip", clips[i]},
                     {"position",
                      {{"x", points[i][0]}, {"y", points[i][1]}, {"note", "retained"}}}});
            return source;
        });
    const auto asset = document->source().asset();
    const auto initial = document->source().document;
    rejects([&] {
        document->edit(document->revision(), "Duplicate point", [](Json& d) {
            d["samples"][0]["position"]["x"] = d["samples"][1]["position"]["x"];
            d["samples"][0]["position"]["y"] = d["samples"][1]["position"]["y"];
        });
    });
    require(document->source().document == initial && !document->can_undo(),
            "Rejected XY edit changed source/history");
    document->edit(document->revision(), "Move point",
                   [](Json& d) { d["samples"][0]["position"]["x"] = -.1; });
    document->undo();
    require(document->source().document == initial, "2D source Undo lost coordinates/extensions");
    AssetImportService imports(lease, animation_blend_space_import_registry(),
                               {"windows", "d3d12", "desktop"}, 1);
    imports.submit(
        imports.prepare(document->locator(), {}, asset),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_animation_blend_space_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    require(imports.wait_idle(15s), "2D publication timeout");
    const auto receipts = imports.poll();
    require(receipts.size() == 1 && receipts[0].published,
            receipts.empty() ? "Missing 2D publication receipt" : receipts[0].diagnostic);
    catalog = AssetCatalog::open_project(project);
    const auto& record = catalog.records().at(asset);
    require(record.metadata.at("forge.import").at("importer_revision") ==
                    "forge.animation.blend-space.v2" &&
                record.metadata.at("forge.import").at("output_version") == 2,
            "2D publication did not use its versioned recipe/wrapper");
    ResourcePool<AnimationBlendSpaceAsset> pool;
    const auto ticket = request_animation_blend_space(pool, project, record);
    require(pool.wait(ticket, 10s) && bool(pool.acquire(ticket)) &&
                pool.acquire(ticket)->space.dimensions == 2,
            "2D typed resource admission failed");
    EngineContext author(WorldRole::Authoring);
    Scene scene(author.world());
    ModelPlacementOptions options;
    options.clip = {base};
    const auto actor = instantiate_model(
        scene, catalog,
        prepare_model_placement(selected, scene.asset_id(), scene.revision(), options));
    const auto before = scene.document();
    auto commands = Json::array();
    const Json settings{{"blend_space", asset}, {"blend_position", .5}, {"blend_position_y", .5}};
    for (const auto& [field, value] : settings.items())
        commands.push_back({{"operation", "property.set"},
                            {"arguments",
                             {{"entity", actor},
                              {"component", "forge.animator"},
                              {"field", field},
                              {"value", value}}}});
    apply_authoring(scene, commands, scene.revision());
    const auto source = scene.document();
    require(scene.undo() && scene.document() == before && scene.redo() &&
                scene.document() == source,
            "2D attachment did not use one Scene history entry");
    World live(project, source, actor), rest_world(project, direct(source, actor, rest), actor),
        base_world(project, direct(source, actor, base), actor),
        overlay_world(project, direct(source, actor, overlay), actor);
    live.tick(.5f);
    std::array<World*, 3> direct_worlds{&rest_world, &base_world, &overlay_world};
    for (auto* world : direct_worlds)
        world->tick(float(world->pose().at("duration").get<double>() * .25));
    const std::array<Json, 3> references{rest_world.pose(), base_world.pose(),
                                         overlay_world.pose()};
    mix_three(references, live.pose(), {.5, .25, .25});
    require(live.pose().at("blend_samples").size() == 3, "Interior pose omitted a sampling layer");
    written_pose(live);
    require(live.animation->control(live.ref(), AnimationControl::Pause, ++live.tick_id),
            "2D pause failed");
    auto config = live.entity().get<Animator>();
    for (const auto& sample : std::array{std::array<double, 5>{.75, .25, .5, .375, .125},
                                         std::array<double, 5>{1.5, 1.5, .25, .375, .375},
                                         std::array<double, 5>{-1, -1, 1, 0, 0},
                                         std::array<double, 5>{.5, .5, .5, .25, .25}}) {
        config.blend_position = float(sample[0]);
        config.blend_position_y = float(sample[1]);
        live.entity().set(config);
        live.animation->synchronize();
        require(live.animation->checkpoint_ready(), "Paused 2D coordinate change failed");
        near(live.pose().at("time"), .5);
        mix_three(references, live.pose(), {sample[2], sample[3], sample[4]});
        written_pose(live);
    }
    const auto checkpoint = live.animation->checkpoint();
    World recovered(project, live.scene.document(), actor, false);
    recovered.animation->restore(checkpoint);
    same(live.pose(), recovered.pose());
    auto invalid = checkpoint;
    invalid["entries"][0]["blend_space"]["position_y"] = .75;
    rejects([&] { recovered.animation->restore(invalid); });
    require(recovered.animation->checkpoint() == checkpoint, "Rejected 2D recovery changed clocks");
    invalid = checkpoint;
    invalid["entries"][0]["blend_space"]["samples"][0]["weight"] = .75;
    rejects([&] { recovered.animation->restore(invalid); });
    SkeletalAnimationPreview preview;
    preview.start(project, source, scene.reference(actor.str()));
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!preview.prepare()) {
        require(std::chrono::steady_clock::now() < deadline, "2D preview admission timeout");
        std::this_thread::sleep_for(1ms);
    }
    preview.select_blend_position(.75f, .25f);
    preview.seek(.5);
    mix_three(references, preview.pose(), {.5, .375, .125});
    require(scene.document() == source, "2D preview changed authored data");
    preview.stop();
    plane_root_motion(project, source, actor);
    plane_rig(project, source, actor, selected);
    plane_budget(project, lease);
    const auto publish = [&] {
        document->save();
        imports.submit(
            imports.prepare(document->locator(), {}, asset),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_animation_blend_space_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(15s), "2D replacement publication timeout");
        return imports.poll();
    };
    document->edit(document->revision(), "Invalid desired clip",
                   [](Json& d) { d["samples"][0]["clip"] = AssetId::generate(); });
    const auto rejected = publish();
    require(rejected.size() == 1 && !rejected[0].published &&
                AssetCatalog::open_project(project).records().at(asset).metadata == record.metadata,
            "Invalid 2D publication changed last-good selection");
    document->undo();
    document->edit(document->revision(), "Longer 2D cycle",
                   [](Json& d) { d["cycle_seconds"] = 4; });
    const auto revised = publish();
    require(revised.size() == 1 && revised[0].published, "2D replacement failed");
    require(
        recovered.animation->control(recovered.ref(), AnimationControl::Play, ++recovered.tick_id),
        "2D reload fixture failed to resume its paused checkpoint");
    recovered.animation->catalog(
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    const auto replacement_deadline = std::chrono::steady_clock::now() + 10s;
    double prior_phase = 0;
    do {
        prior_phase = recovered.pose().at("time").get<double>() /
                      recovered.pose().at("duration").get<double>();
        recovered.tick(.01f);
        require(std::chrono::steady_clock::now() < replacement_deadline,
                "2D revision admission timeout");
        std::this_thread::sleep_for(1ms);
    } while (recovered.pose().at("duration") != 4);
    near(recovered.pose().at("time"), prior_phase * 4 + double(.01f));
    const auto held = recovered.animation->checkpoint();
    rejects([&] { recovered.animation->restore(checkpoint); });
    require(recovered.animation->checkpoint() == held,
            "Stale 2D recovery changed admitted revision");
    std::cout << "Actual three-layer pose/morph/native channel, paused triangle changes, recovery "
                 "and XY preview passed\n";
}
