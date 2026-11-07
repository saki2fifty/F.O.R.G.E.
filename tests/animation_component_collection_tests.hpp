#pragma once
struct CollectionMotion {
    PaddedOwnedState rows[2];
    float weights[2]{.25f, .5f};
    std::int64_t markers[2]{};
    MotionMode modes[2]{MotionMode::Forward, MotionMode::Forward};
    std::uint32_t flags[2]{3, 3};
    bool enabled[2]{true, true};
    float gain = 1;
    float matrix[2][2]{};
};
struct CollectionEntry {
    std::string slot, name;
    float speed = 45;
    bool enabled = true;
    std::uint16_t hidden = 11;
    std::int64_t marker = 0;
    MotionMode mode = MotionMode::Forward;
    std::uint32_t flags = 3;
    float weights[2]{1, 1};
    std::string note = "retained";
};
EngineModule collection_module() {
    auto module = motion_module();
    const auto base = module.schemas;
    module.schemas = [base](ModuleContext& context) {
        base(context);
        auto& world = context.world;
        world.component<PaddedOwnedState>()
            .member<std::string>("label")
            .member<float>("speed")
            .member<bool>("enabled");
        const auto mask = world.lookup("project.MotionFlags");
        check(bool(mask), "Collection flags type is unavailable");
        const auto array = world.component<CollectionMotion>();
        ecs_struct_desc_t descriptor{};
        descriptor.entity = array;
        const std::array names{"rows",  "weights", "markers", "modes",
                               "flags", "enabled", "gain",    "matrix"};
        const std::array<ecs_entity_t, 8> types{world.id<PaddedOwnedState>(),
                                                world.id<float>(),
                                                world.id<std::int64_t>(),
                                                world.id<MotionMode>(),
                                                mask,
                                                world.id<bool>(),
                                                world.id<float>(),
                                                world.array<float>(2)};
        const std::array offsets{
            offsetof(CollectionMotion, rows),    offsetof(CollectionMotion, weights),
            offsetof(CollectionMotion, markers), offsetof(CollectionMotion, modes),
            offsetof(CollectionMotion, flags),   offsetof(CollectionMotion, enabled),
            offsetof(CollectionMotion, gain),    offsetof(CollectionMotion, matrix)};
        for (std::size_t i = 0; i < names.size(); ++i) {
            auto& member = descriptor.members[i];
            member.name = names[i];
            member.type = types[i];
            member.count = i == 6 ? 0 : 2;
            member.offset = std::int32_t(offsets[i]);
            member.use_offset = true;
        }
        check(ecs_struct_init(world, &descriptor) == array, "Collection array registration failed");
        detail::opt_in_authoring(
            world, array, "project.array_motion", "project.motion", 1,
            {{"rows", Json::array({{{"label", "left"}, {"speed", 20}, {"enabled", true}},
                                   {{"label", "right"}, {"speed", 45}, {"enabled", true}}})},
             {"weights", {.25, .5}},
             {"markers", {0, 0}},
             {"modes", {1, 1}},
             {"flags", {3, 3}},
             {"enabled", {true, true}},
             {"gain", 1},
             {"matrix", {{0, 0}, {0, 0}}}},
            "Animation");
        const auto entry = world.component<CollectionEntry>();
        ecs_struct_desc_t entry_metadata{};
        entry_metadata.entity = entry;
        entry_metadata.create_member_entities = true;
        const std::array entry_names{"slot", "name",  "speed",   "enabled", "marker",
                                     "mode", "flags", "weights", "note"};
        const std::array<ecs_entity_t, 9> entry_types{world.id<std::string>(),
                                                      world.id<std::string>(),
                                                      world.id<float>(),
                                                      world.id<bool>(),
                                                      world.id<std::int64_t>(),
                                                      world.id<MotionMode>(),
                                                      mask,
                                                      world.id<float>(),
                                                      world.id<std::string>()};
        const std::array entry_offsets{
            offsetof(CollectionEntry, slot),   offsetof(CollectionEntry, name),
            offsetof(CollectionEntry, speed),  offsetof(CollectionEntry, enabled),
            offsetof(CollectionEntry, marker), offsetof(CollectionEntry, mode),
            offsetof(CollectionEntry, flags),  offsetof(CollectionEntry, weights),
            offsetof(CollectionEntry, note)};
        for (std::size_t i = 0; i < entry_names.size(); ++i) {
            auto& member = entry_metadata.members[i];
            member.name = entry_names[i];
            member.type = entry_types[i];
            member.count = i == 7 ? 2 : 0;
            member.offset = std::int32_t(entry_offsets[i]);
            member.use_offset = true;
        }
        entry_metadata.members[2].range = {-200, 200};
        check(ecs_struct_init(world, &entry_metadata) == entry,
              "Collection entry registration failed");
        const auto vector = world.vector<CollectionEntry>();
        const auto root = world.entity("CollectionVectorRoot");
        ecs_struct_desc_t table{};
        table.entity = root;
        table.create_member_entities = true;
        const std::array fields{"items", "single", "positional"};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            table.members[i].name = fields[i];
            table.members[i].type = vector;
        }
        check(ecs_struct_init(world, &table) == root, "Collection vector registration failed");
        world.component<detail::ReflectedSequenceKey>(detail::reflected_sequence_key_type);
        world.component<detail::ReflectedSequenceLimit>(detail::reflected_sequence_limit_type);
        root.lookup("items").set<detail::ReflectedSequenceKey>({"slot", {"name"}});
        root.lookup("single").set<detail::ReflectedSequenceKey>({"slot"});
        for (const auto* name : fields)
            root.lookup(name).set<detail::ReflectedSequenceLimit>({4});
        const auto rows = Json::array({{{"slot", "left"},
                                        {"name", "speed"},
                                        {"speed", 45},
                                        {"enabled", true},
                                        {"marker", 0},
                                        {"mode", 1},
                                        {"flags", 3},
                                        {"weights", {1, 1}},
                                        {"note", "retained"}},
                                       {{"slot", "right"},
                                        {"name", "speed"},
                                        {"speed", 60},
                                        {"enabled", true},
                                        {"marker", 0},
                                        {"mode", 1},
                                        {"flags", 3},
                                        {"weights", {1, 1}},
                                        {"note", "retained"}}});
        detail::opt_in_authoring(world, root, "project.vector_motion", "project.motion", 1,
                                 {{"items", rows}, {"single", rows}, {"positional", rows}},
                                 "Animation");
    };
    return module;
}
void collection_playback(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    EngineContext authored(WorldRole::Authoring, false, {collection_module()});
    Scene scene(authored.world());
    const auto codec = [&](const auto& context, const char* key) -> const detail::AuthoredCodec& {
        const auto& values = context.authored_codecs();
        const auto at = std::find_if(values.begin(), values.end(),
                                     [&](const auto& c) { return c.key() == key; });
        check(at != values.end(), "Collection codec is unavailable");
        return *at;
    };
    const auto& arrays = codec(authored.world(), "project.array_motion");
    const auto& vectors = codec(authored.world(), "project.vector_motion");
    using Kind = AnimationComponentValueType;
    const auto field = [](const auto& owner, const char* root_field, Kind kind,
                          std::vector<Json> suffix) {
        return AnimationComponentBinding{owner.key(), root_field, owner.stamp(),
                                         kind,        {},         std::move(suffix)};
    };
    const auto speed = field(arrays, "rows", Kind::Scalar, {{{"index", 0}}, "speed"});
    const auto weight = field(arrays, "weights", Kind::Scalar, {{{"index", 1}}});
    const auto marker = field(arrays, "markers", Kind::SignedInteger, {{{"index", 0}}});
    const auto mode = field(arrays, "modes", Kind::SignedInteger, {{{"index", 1}}});
    const auto flags = field(arrays, "flags", Kind::UnsignedInteger, {{{"index", 0}}});
    const auto enabled = field(arrays, "enabled", Kind::Boolean, {{{"index", 1}}});
    const auto keyed =
        field(vectors, "items", Kind::Scalar, {{{"key", {"left", "speed"}}}, "speed"});
    const auto single =
        field(vectors, "single", Kind::SignedInteger, {{{"key", "right"}}, "marker"});
    const auto positional =
        field(vectors, "positional", Kind::Boolean, {{{"index", 1}}, "enabled"});
    const auto matrix = field(arrays, "matrix", Kind::Scalar, {{{"index", 1}}, {{"index", 0}}});
    const auto nested_collection = field(vectors, "items", Kind::Scalar,
                                         {{{"key", {"left", "speed"}}}, "weights", {{"index", 1}}});
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Collections.animation.json");
    auto source = document->source();
    source.add_component_field(speed, 20);
    source.add_component_field(weight, .5);
    source.add_component_field(marker, INT64_MIN);
    source.add_component_field(mode, 1);
    source.add_component_field(flags, 1u);
    source.add_component_field(enabled, true);
    source.add_component_field(keyed, 45);
    source.add_component_field(single, (std::int64_t{1} << 53) + 1);
    source.add_component_field(positional, true);
    source.add_component_field(matrix, 10);
    source.add_component_field(nested_collection, 1);
    source.document["channels"] = Json::array();
    const auto first = source.clip().keys.front().id;
    const auto last = source.set_key(2 * animation_timebase, {});
    source.set_component_value(last, speed, 100);
    source.set_component_curve(first, speed, {true, 0, 120});
    source.set_component_value(last, weight, 1);
    source.set_component_value(last, marker, INT64_MAX);
    source.set_component_value(last, mode, -1);
    source.set_component_value(last, flags, 2u);
    source.set_component_value(last, enabled, false);
    source.set_component_value(last, keyed, 135);
    source.set_component_value(last, single, INT64_MAX);
    source.set_component_value(last, positional, false);
    source.set_component_value(last, matrix, 20);
    source.set_component_value(last, nested_collection, 3);
    check(source.document.at("version") == 9, "Collection source needs explicit promotion");
    const auto clip = decode_transform_animation(source.cook(), source.asset());
    check(clip.component_bindings == source.clip().component_bindings,
          "Collection cook changed stable paths");
    const auto before = source.document;
    for (const Json& suffix :
         {Json::array({"speed"}), Json::array({{{"index", -1}}}), Json::array({{{"index", 1.25}}}),
          Json::array({{{"index", 4096}}}), Json::array({{{"index", 0}, {"key", "left"}}}),
          Json::array({{{"key", Json::array()}}})}) {
        auto bad = source;
        bad.document["component_bindings"][0]["element_path"] = suffix;
        rejects([&] { bad.validate(); });
    }
    for (const bool descriptor : {false, true}) {
        auto legacy = TransformAnimationSource::create(AssetId::generate());
        legacy.add_component_field({arrays.key(), "gain", arrays.stamp()}, 1);
        legacy.document["version"] = 8;
        legacy.document["component_bindings"][0]["value_type"] = "scalar";
        (descriptor ? legacy.document["component_bindings"][0]
                    : legacy.document["keys"][0]["component_values"][0])["element_path"] = "opaque";
        const auto retained = legacy.document;
        rejects([&] { legacy.add_component_field(speed, 20); });
        check(legacy.document == retained, "Collection promotion changed opaque legacy bytes");
    }
    auto bad = source;
    bad.set_component_value(last, keyed, 300);
    rejects([&] {
        sample_animation_component(vectors.editor_schema(), vectors.defaults(), bad.clip(), 0,
                                   true);
    });
    auto missing = keyed;
    missing.element_path[0] = {{"key", {"absent", "speed"}}};
    auto wrong = keyed;
    wrong.element_path[0] = {{"index", 0}};
    for (const auto& invalid : {missing, wrong}) {
        auto invalid_source = source;
        invalid_source.erase_component_field(keyed);
        invalid_source.add_component_field(invalid, 90);
        rejects([&] {
            sample_animation_component(vectors.editor_schema(), vectors.defaults(),
                                       invalid_source.clip(), 0, true);
        });
    }
    auto overshoot = source;
    overshoot.set_component_curve(first, keyed, {true, 0, 1200});
    rejects([&] {
        sample_animation_component(vectors.editor_schema(), vectors.defaults(), overshoot.clip(), 0,
                                   true);
    });
    auto absent_index = weight;
    absent_index.element_path[0] = {{"index", 2}};
    auto wrong_key_form = keyed;
    wrong_key_form.element_path[0] = {{"key", "left"}};
    for (const auto& invalid : {absent_index, wrong_key_form}) {
        auto invalid_source = source;
        invalid_source.add_component_field(invalid, 90);
        const auto& owner = invalid.component == arrays.key() ? arrays : vectors;
        rejects([&] {
            sample_animation_component(owner.editor_schema(), owner.defaults(),
                                       invalid_source.clip(), 0, true);
        });
    }
    auto duplicated = source;
    const std::array duplicate_keys{first};
    duplicated.duplicate_keys(duplicate_keys, animation_timebase);
    duplicated.retime_duration(4 * animation_timebase);
    check(duplicated.clip().keys[1].component_values[2].value == INT64_MIN &&
              duplicated.clip().component_bindings == clip.component_bindings,
          "Collection duplication/retiming lost identity or exact values");
    auto readonly = arrays.editor_schema();
    readonly["fields"][1]["read_only"] = true;
    rejects([&] { sample_animation_component(readonly, arrays.defaults(), clip, 0, true); });
    check(source.document == before, "Invalid collection tests changed source");
    document->edit(document->revision(), "Collection fields",
                   [&](auto& json) { json = source.document; });
    document->undo();
    check(document->source().clip().component_bindings.empty(), "Collection Undo failed");
    document->redo();
    document->save();
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    importer.submit(
        importer.prepare(document->locator(), {}, source.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(importer.wait_idle(10s), "Collection publication stalled");
    const auto results = importer.poll();
    check(results.size() == 1 && results[0].published, "Collection publication failed");
    const auto catalog = std::make_shared<const AssetCatalog>(results[0].publication->catalog);
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Collection animation"}})
                           .at("selected")
                           .get<std::string>();
    auto entity = scene.entity(actor);
    arrays.apply(entity, arrays.prepare(scene.world(), arrays.defaults()));
    vectors.apply(entity, vectors.prepare(scene.world(), vectors.defaults()));
    TransformAnimator animator;
    animator.clip = {source.asset()};
    animator.loop = false;
    entity.set(animator);
    auto saved = scene.snapshot();
    saved["entities"][0]["components"][vectors.key()]["items"][0]["future"] = "left metadata";
    scene.restore_snapshot(saved);
    auto preview = scene.effective_document();
    const auto schema = scene.schema();
    check(apply_transform_animation_preview(preview, {scene.asset_id(), EntityId::parse(actor)},
                                            clip, animation_timebase, &schema),
          "Collection copied preview failed");
    near(preview["entities"][0]["components"][arrays.key()]["rows"][0]["speed"], 90);
    check(preview["entities"][0]["components"][arrays.key()]["markers"][0] == INT64_MIN,
          "Collection preview rounded held64 value");
    Runtime live(root, saved, catalog, collection_module());
    live.ready();
    auto runtime_entity = live.scene.entity(actor);
    auto array_state = runtime_entity.get<CollectionMotion>();
    array_state.rows[0].counter = 73;
    array_state.rows[1].counter = 83;
    array_state.rows[1].label = std::string(96, 'a');
    runtime_entity.set(array_state);
    const auto& runtime_vector = codec(live.engine.world(), vectors.key().c_str());
    auto native =
        runtime_vector.prepare_update(runtime_entity, runtime_vector.read(runtime_entity, true));
    const auto entries = [&](const void* root_value) {
        const auto* native_schema =
            ecs_get(runtime_entity.world(), runtime_vector.native_type, EcsStruct);
        const auto* members =
            static_cast<const ecs_member_t*>(ecs_vec_first(&native_schema->members));
        const auto* storage = reinterpret_cast<const ecs_vec_t*>(
            static_cast<const std::byte*>(root_value) + members[0].offset);
        return static_cast<const CollectionEntry*>(ecs_vec_first(storage));
    };
    auto* rows = const_cast<CollectionEntry*>(entries(native->data()));
    rows[0].hidden = 73;
    rows[1].hidden = 83;
    rows[0].note = std::string(96, 'v');
    std::swap(rows[0], rows[1]);
    runtime_vector.apply(runtime_entity, native);
    live.simulation.tick(1.f);
    near(runtime_entity.get<CollectionMotion>().rows[0].speed, 90);
    near(runtime_entity.get<CollectionMotion>().weights[1], .75);
    near(runtime_entity.get<CollectionMotion>().matrix[1][0], 15);
    check(runtime_entity.get<CollectionMotion>().markers[0] == INT64_MIN &&
              runtime_entity.get<CollectionMotion>().rows[0].counter == 73 &&
              runtime_entity.get<CollectionMotion>().rows[1].counter == 83 &&
              runtime_entity.get<CollectionMotion>().rows[1].label == array_state.rows[1].label,
          "Collection playback changed neighboring or hidden array state");
    const auto native_value = runtime_vector.read(runtime_entity, true);
    near(native_value["items"][1]["speed"], 90);
    near(native_value["items"][1]["weights"][1], 2);
    check(native_value["items"][1]["slot"] == "left" &&
              entries(
                  ecs_get_id(runtime_entity.world(), runtime_entity, runtime_vector.native_type))[1]
                      .hidden == 73,
          "Keyed animation retargeted or reset a reordered entry");
    const auto presented = live.simulation.presentation(.5);
    check(presented["entities"][0]["components"][vectors.key()]["items"][1]["future"] ==
              "left metadata",
          "Collection projection lost keyed opaque metadata");
    const auto array_before =
        codec(live.engine.world(), arrays.key().c_str()).read(runtime_entity, true);
    rejects([&] {
        prepare_animation_components(live.engine.world(), runtime_entity, overshoot.clip(), 0,
                                     true);
    });
    check(codec(live.engine.world(), arrays.key().c_str()).read(runtime_entity, true) ==
                  array_before &&
              runtime_vector.read(runtime_entity, true) == native_value,
          "Rejected collection interval mutated a neighboring component");
    auto missing_value = native_value;
    missing_value["items"].erase(missing_value["items"].begin() + 1);
    runtime_vector.apply(runtime_entity,
                         runtime_vector.prepare_update(runtime_entity, missing_value));
    rejects([&] {
        prepare_animation_components(live.engine.world(), runtime_entity, clip, animation_timebase,
                                     true);
    });
    check(runtime_vector.read(runtime_entity, true)["items"].size() == 1 &&
              codec(live.engine.world(), arrays.key().c_str()).read(runtime_entity, true) ==
                  array_before,
          "Missing collection target was recreated or partially applied");
    runtime_vector.apply(runtime_entity,
                         runtime_vector.prepare_update(runtime_entity, native_value));
    auto cleared_value = native_value;
    cleared_value["items"] = Json::array();
    auto cleared = runtime_vector.prepare_update(runtime_entity, cleared_value);
    check(runtime_vector.read(runtime_entity, true) == native_value,
          "Detached vector clearing mutated its source");
    runtime_vector.apply(runtime_entity, cleared);
    check(runtime_vector.read(runtime_entity, true)["items"].empty(),
          "Native vector clearing retained owned entries");
    runtime_vector.apply(runtime_entity,
                         runtime_vector.prepare_update(runtime_entity, native_value));
    Runtime cold(root, live.scene.snapshot(), catalog, collection_module());
    cold.ready();
    cold.animation->restore(live.animation->checkpoint());
    for (auto* runtime : {&live, &cold}) {
        runtime->simulation.tick(1.f);
        const auto& current = runtime->scene.entity(actor).get<CollectionMotion>();
        check(current.markers[0] == INT64_MAX && current.modes[1] == MotionMode::Reverse &&
                  current.flags[0] == 2 && !current.enabled[1],
              "Collection endpoint/recovery changed held native values");
    }
    const EntityRef reference{live.scene.asset_id(), EntityId::parse(actor)};
    AnimationPlaybackAccess::control(*live.animation, reference, {{"operation", "pause"}});
    live.simulation.tick(.25f);
    check(runtime_entity.get<CollectionMotion>().markers[0] == INT64_MAX,
          "Paused collection changed");
    AnimationPlaybackAccess::control(*live.animation, reference,
                                     {{"operation", "seek"}, {"position_seconds", 1}});
    check(runtime_entity.get<CollectionMotion>().markers[0] == INT64_MIN,
          "Collection seek lost exact held value");
    auto removed = source;
    removed.erase_component_field(matrix);
    check(removed.clip().component_bindings.size() + 1 == clip.component_bindings.size(),
          "Collection binding removal deleted siblings");
    check(scene.snapshot() == saved, "Collection playback changed authored scene");
    const auto controller =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 4;
    scene.entity(controller).set(sequence);
    animator.sequence = {scene.asset_id(), EntityId::parse(controller)};
    animator.sequence_next_clip = animator.clip;
    animator.sequence_next_start_seconds = 1;
    scene.entity(actor).set(animator);
    const auto pair_saved = scene.snapshot();
    Runtime pair(root, pair_saved, catalog, collection_module());
    pair.ready();
    const auto target = pair.scene.entity(actor);
    auto reordered = vectors.read(target, true);
    std::reverse(reordered["items"].begin(), reordered["items"].end());
    vectors.apply(target, vectors.prepare_update(target, reordered));
    AnimationPlaybackAccess::control(*pair.animation, animator.sequence,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    near(target.get<CollectionMotion>().rows[0].speed, 82.5);
    near(target.get<CollectionMotion>().matrix[1][0], 15);
    const auto values = vectors.read(target, true);
    const auto& items = values.at("items");
    check(items.at(0).at("slot") == "right" && items.at(1).at("slot") == "left",
          "Sequence blend reordered the native keyed collection");
    near(items.at(1).at("speed"), 90);
    near(items.at(1).at("weights")[1], 2);
    near(items.at(0).at("speed"), 60);
    check(target.get<CollectionMotion>().markers[0] == INT64_MIN &&
              items.at(1).at("note") == "retained" && scene.snapshot() == pair_saved,
          "Sequence collection blend changed held, unanimated or authored values");
}
