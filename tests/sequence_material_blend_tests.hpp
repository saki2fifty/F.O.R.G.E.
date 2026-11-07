#pragma once
void sequence_material_blends(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    EngineContext authored;
    Scene scene(authored.world());
    const auto schema = scene.schema();
    const auto& declarations = schema.at("components");
    const auto ui = std::find_if(declarations.begin(), declarations.end(), [](const auto& value) {
        return value.at("id") == "forge.ui_values";
    });
    check(ui != declarations.end(), "Mixed sequence requires UI Values declaration");
    auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    const auto publish = [&](TransformAnimationDocument& document) {
        document.save();
        importer.submit(
            importer.prepare(document.locator(), {}, document.source().asset()),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(importer.wait_idle(10s), "Sequence Material publication stalled");
        const auto receipts = importer.poll();
        check(receipts.size() == 1 && receipts.front().published,
              "Sequence Material publication failed");
        return std::make_shared<const AssetCatalog>(receipts.front().publication->catalog);
    };
    const auto slot = asset_detail::engine_mesh_resource(engine_primitive(0)).materials.front().key;
    const auto make = [&](const char* path, bool incoming, unsigned invalid = 0) {
        auto document = TransformAnimationDocument::create(lease, path);
        auto source = document->source();
        source.retime_duration(2 * animation_timebase);
        source.add_material_parameter({slot, "roughnessFactor", MaterialParameterType::Scalar},
                                      {invalid == 1 ? 1.2
                                       : incoming   ? .8
                                                    : .2});
        source.add_material_parameter({slot, "metallicFactor", MaterialParameterType::Scalar},
                                      {incoming ? .6 : 0});
        source.add_material_parameter(
            {slot, "baseColorFactor",
             invalid == 2 ? MaterialParameterType::Vector4 : MaterialParameterType::LinearColor4},
            incoming ? AnimationMaterialValues{.7, .5, .1, 1}
                     : AnimationMaterialValues{.1, .2, .3, 1});
        source.add_property(AnimationProperty::LightIntensity, incoming ? 6 : 2);
        source.add_component_field({"forge.ui_values",
                                    "values",
                                    ui->at("admission"),
                                    AnimationComponentValueType::Scalar,
                                    {},
                                    {Json{{"key", "amount"}}, "value"}},
                                   incoming ? .8 : .2);
        source.add_component_field({"forge.ui_values",
                                    "values",
                                    ui->at("admission"),
                                    AnimationComponentValueType::Scalar,
                                    {},
                                    {Json{{"key", "amount2"}}, "value"}},
                                   incoming ? .4 : .1);
        source.document["channels"] = Json::array();
        if (incoming) {
            std::reverse(source.document["material_bindings"].begin(),
                         source.document["material_bindings"].end());
            for (auto& key : source.document["keys"])
                std::reverse(key["material_values"].begin(), key["material_values"].end());
            std::reverse(source.document["component_bindings"].begin(),
                         source.document["component_bindings"].end());
            for (auto& key : source.document["keys"])
                std::reverse(key["component_values"].begin(), key["component_values"].end());
        }
        document->edit(document->revision(), "Sequence material clip",
                       [&](auto& value) { value = source.document; });
        (void)publish(*document);
        return source.asset();
    };
    const auto first = make("First.animation.json", false);
    const auto next = make("Next.animation.json", true);
    const auto bad = make("Invalid.animation.json", true, 1);
    const auto semantic = make("Semantic.animation.json", true, 2);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto controller =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 4;
    scene.entity(controller).set(sequence);
    const auto actor = authoring_command(scene, "entity.create").at("selected").get<std::string>();
    scene.entity(actor)
        .set<MeshRenderer>({engine_primitive(0)})
        .set<Light>({})
        .set<UiValues>({{{"amount", .2}, {"amount2", .1}, {"manual", .75}}});
    TransformAnimator track{{first}};
    track.sequence = {scene.asset_id(), EntityId::parse(controller)};
    track.sequence_next_clip = {next};
    track.sequence_next_start_seconds = 1;
    scene.entity(actor).set(track);
    const auto saved = scene.document();
    World live(root, saved);
    live.animation->catalog(catalog);
    live.ready("Material sequence pair");
    const auto entity = live.scene.entity(actor);
    const auto verify = [&](flecs::entity target, double weight) {
        const auto& renderer = target.get<MeshRenderer>();
        near(roughness(renderer), .2 + .6 * weight);
        const auto& values = renderer.material_parameters;
        const auto at = std::find_if(values.begin(), values.end(), [](const auto& value) {
            return value.key == "metallicFactor";
        });
        check(at != values.end(), "Material pair lost its second binding");
        near(at->parameter.value[0], .6 * weight);
        const auto color = std::find_if(values.begin(), values.end(), [](const auto& value) {
            return value.key == "baseColorFactor";
        });
        check(color != values.end() && color->parameter.type == MaterialParameterType::LinearColor4,
              "Material pair lost its color semantic");
        near(color->parameter.value[0], .1 + .6 * weight);
        near(color->parameter.value[1], .2 + .3 * weight);
        near(color->parameter.value[2], .3 - .2 * weight);
        near(color->parameter.value[3], 1);
        const auto& fields = target.get<UiValues>().values;
        check(fields.size() == 3, "Mixed sequence resized reflected fields");
        for (const auto& value : fields)
            near(value.value, value.name == "amount"    ? .2 + .6 * weight
                              : value.name == "amount2" ? .1 + .3 * weight
                                                        : .75);
    };
    {
        auto collection = track;
        for (std::size_t i = 2; i < sequence_clip_limit; ++i)
            collection.sequence_extra_clips.push_back({{i % 2 ? next : first}, double(i)});
        auto longer = sequence;
        longer.duration = 10;
        scene.entity(controller).set(longer);
        scene.entity(actor).set(collection);
        const auto collection_source = scene.document();
        scene.restore_snapshot(saved);
        World arranged(root, collection_source);
        arranged.animation->catalog(catalog);
        arranged.ready("Eight-clip Material/reflected collection");
        for (const auto [time, expected] : std::array<std::array<double, 2>, 7>{{{1.5, .5},
                                                                                 {2.25, .75},
                                                                                 {3.75, .75},
                                                                                 {4.25, .75},
                                                                                 {5.5, .5},
                                                                                 {7.75, .75},
                                                                                 {8.5, 1}}}) {
            AnimationPlaybackAccess::control(*arranged.animation, track.sequence,
                                             {{"operation", "seek"}, {"position_seconds", time}});
            verify(arranged.scene.entity(actor), expected);
            near(arranged.scene.entity(actor).get<Light>().intensity, 2 + 4 * expected);
        }
        const auto recovered_collection = arranged.animation->checkpoint();
        World restored(root, collection_source);
        restored.animation->catalog(catalog);
        restored.animation->restore(recovered_collection);
        verify(restored.scene.entity(actor), 1);
        // An incompatible future-only binding must reject the complete candidate.
        const auto extra = make("OnlyExtra.animation.json", true);
        collection.sequence_extra_clips.back().clip = {extra};
        scene.entity(controller).set(longer);
        scene.entity(actor).set(collection);
        const auto extra_source = scene.document();
        scene.restore_snapshot(saved);
        World future(root, extra_source);
        future.animation->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        future.ready("Future-only extra Material clip");
        const auto before_revision = future.scene.snapshot();
        TransformAnimationDocument replacement(lease, "OnlyExtra.animation.json");
        replacement.edit_source(replacement.revision(), "Future extra revision", [&](auto& source) {
            source.set_material_value(source.clip().keys.front().id, slot, "roughnessFactor",
                                      {1.2});
        });
        future.animation->catalog(publish(replacement));
        rejects([&] { future.ready("Invalid future-only extra revision"); });
        check(future.scene.snapshot() == before_revision,
              "Future extra Material revision partially published current fields");
        replacement.edit_source(replacement.revision(), "Future extra repair", [&](auto& source) {
            source.set_material_value(source.clip().keys.front().id, slot, "roughnessFactor", {.4});
        });
        future.animation->catalog(publish(replacement));
        future.ready("Corrected future-only extra revision");
        AnimationPlaybackAccess::control(*future.animation, track.sequence,
                                         {{"operation", "seek"}, {"position_seconds", 8.5}});
        near(roughness(future.scene.entity(actor).get<MeshRenderer>()), .4);
        check(!future.animation->playback(track.sequence)->finished,
              "Collection child endpoint finished its longer controller");
    }
    live.simulation.tick(1.f);
    live.simulation.tick(.5f);
    verify(entity, .5);
    near(entity.get<Light>().intensity, 4);
    const auto native = entity.get<MeshRenderer>();
    const auto preview = live.simulation.presentation(.5);
    bool found = false;
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            const auto& values =
                row.at("components").at("forge.mesh_renderer").at("material_parameters");
            for (const auto& value : values)
                if (value.at("key") == "baseColorFactor") {
                    const auto& color = value.at("parameter").at("value");
                    near(color[0], .25);
                    near(color[1], .275);
                    near(color[2], .25);
                    near(color[3], 1);
                } else
                    near(value.at("parameter").at("value")[0],
                         value.at("key") == "roughnessFactor" ? .35 : .15);
            near(row.at("components").at("forge.light").at("intensity"), 3);
            const auto& fields = row.at("components").at("forge.ui_values").at("values");
            check(fields.size() == 3, "Mixed presentation resized reflected fields");
            for (const auto& value : fields)
                near(value.at("value"), value.at("name") == "amount"    ? .35
                                        : value.at("name") == "amount2" ? .175
                                                                        : .75);
            found = true;
        }
    check(found && entity.get<MeshRenderer>() == native,
          "Material pair presentation changed native values");
    const auto checkpoint = live.animation->checkpoint();
    World recovered(root, live.scene.snapshot());
    recovered.animation->catalog(catalog);
    recovered.ready("Recovered material sequence pair");
    recovered.animation->restore(checkpoint);
    verify(recovered.scene.entity(actor), .5);
    for (const auto& [time, weight] :
         std::array{std::pair{0., 0.}, std::pair{1.25, .25}, std::pair{1.5, .5}, std::pair{2., 1.},
                    std::pair{3.5, 1.}}) {
        AnimationPlaybackAccess::control(*live.animation, track.sequence,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        verify(entity, weight);
        near(entity.get<Light>().intensity, 2 + 4 * weight);
    }
    for (const auto invalid : {bad, semantic}) {
        track.sequence_next_clip = {invalid};
        scene.entity(actor).set(track);
        World rejected(root, scene.snapshot());
        rejected.animation->catalog(catalog);
        const auto before = rejected.scene.snapshot();
        rejects([&] { rejected.ready("Invalid incoming Material curve or semantic"); });
        check(rejected.scene.snapshot() == before && saved != before,
              "Invalid inactive incoming Material clip changed native values");
    }
    AnimationPlaybackAccess::control(*live.animation, track.sequence,
                                     {{"operation", "seek"}, {"position_seconds", 0}});
    const auto before_bad_revision = live.scene.snapshot();
    TransformAnimationDocument replacement(lease, "Next.animation.json");
    const auto replace = [&](double roughness_value) {
        replacement.edit_source(replacement.revision(), "Incoming Material revision",
                                [&](auto& source) {
                                    source.set_material_value(source.clip().keys.front().id, slot,
                                                              "roughnessFactor", {roughness_value});
                                });
        live.animation->catalog(publish(replacement));
    };
    replace(1.2);
    rejects([&] { live.ready("Invalid incoming replacement while inactive"); });
    check(live.scene.snapshot() == before_bad_revision,
          "Incoming-only revision bypassed admission or changed last accepted values");
    replace(.4);
    live.ready("Corrected incoming replacement");
    AnimationPlaybackAccess::control(*live.animation, track.sequence,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    near(roughness(entity.get<MeshRenderer>()), .3);
    near(entity.get<Light>().intensity, 4);
    const auto before_restore = live.scene.snapshot();
    const auto before_restore_clock = live.animation->checkpoint();
    rejects([&] { live.animation->restore(checkpoint); });
    check(live.scene.snapshot() == before_restore &&
              live.animation->checkpoint() == before_restore_clock,
          "Recovery accepted old incoming revision or partially changed native values");
}
