#pragma once
void sequence_component_blends(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    using Kind = AnimationComponentValueType;
    EngineContext authored(WorldRole::Authoring, false, {motion_module()});
    Scene scene(authored.world());
    const auto& codec = authored.world().authored_codecs().front();
    const auto binding = [&](const char* field, Kind type) {
        return AnimationComponentBinding{codec.key(), field, codec.stamp(), type};
    };
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
        check(importer.wait_idle(10s), "Sequence component clip publication stalled");
        const auto receipts = importer.poll();
        check(receipts.size() == 1 && receipts.front().published,
              "Sequence component clip publication failed");
        return std::make_shared<const AssetCatalog>(receipts.front().publication->catalog);
    };
    const auto make = [&](const char* path, bool incoming, unsigned invalid = 0) {
        auto document = TransformAnimationDocument::create(lease, path);
        auto source = document->source();
        source.retime_duration(2 * animation_timebase);
        source.add_component_field(binding("speed", Kind::Scalar), incoming ? 100 : 20);
        source.add_component_field(binding("enabled", Kind::Boolean), !incoming);
        source.add_component_field(binding("marker", Kind::SignedInteger),
                                   incoming ? INT64_MAX : INT64_MIN);
        source.add_component_field(binding("score", Kind::UnsignedInteger),
                                   incoming ? (std::uint64_t{1} << 53) + 1 : UINT64_MAX);
        source.add_component_field(binding("ticks", Kind::SignedInteger), incoming ? 6 : 5);
        source.document["channels"] = Json::array();
        const auto last = source.set_key(2 * animation_timebase, {});
        source.set_component_value(last, codec.key(), "speed", incoming ? 120 : 40);
        if (invalid == 1)
            source.erase_component_field(codec.key(), "score");
        if (invalid == 2)
            source.set_component_value(last, codec.key(), "ticks", INT64_MAX);
        if (invalid == 3)
            for (auto& field : source.document["component_bindings"])
                field["admission"]["digest"] = std::string(64, '1');
        if (incoming) {
            std::reverse(source.document["component_bindings"].begin(),
                         source.document["component_bindings"].end());
            for (auto& key : source.document["keys"])
                std::reverse(key["component_values"].begin(), key["component_values"].end());
        }
        document->edit(document->revision(), "Sequence field clip",
                       [&](auto& value) { value = source.document; });
        (void)publish(*document);
        return source.asset();
    };
    const auto first = make("First.animation.json", false);
    const auto next = make("Next.animation.json", true);
    const auto missing = make("Missing.animation.json", true, 1);
    const auto range = make("Range.animation.json", true, 2);
    const auto stamp = make("Schema.animation.json", true, 3);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto controller =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 4;
    scene.entity(controller).set(sequence);
    const auto actor = authoring_command(scene, "entity.create").at("selected").get<std::string>();
    codec.apply(scene.entity(actor), codec.prepare(scene.world(), codec.defaults()));
    TransformAnimator track{{first}};
    track.sequence = {scene.asset_id(), EntityId::parse(controller)};
    track.sequence_next_clip = {next};
    track.sequence_next_start_seconds = 1;
    scene.entity(actor).set(track);
    const auto saved = scene.snapshot();
    Runtime live(root, saved, catalog);
    live.ready();
    const auto entity = live.scene.entity(actor);
    const auto verify = [&](const Motion& value, double speed, bool incoming) {
        near(value.speed, speed);
        check(value.enabled == !incoming && value.marker == (incoming ? INT64_MAX : INT64_MIN) &&
                  value.score == (incoming ? (std::uint64_t{1} << 53) + 1 : UINT64_MAX) &&
                  value.ticks == (incoming ? 6 : 5) && value.label == "retained" &&
                  value.weight == .5,
              "Sequence blend rounded discrete values or changed an unanimated field");
    };
    {
        auto layered = track;
        layered.sequence_multiway = true;
        layered.sequence_next_start_seconds = .5;
        layered.sequence_extra_clips = {{{first}, 1.}};
        scene.entity(actor).set(layered);
        const auto layered_source = scene.snapshot();
        scene.restore_snapshot(saved);
        Runtime mixed(root, layered_source, catalog);
        mixed.ready();
        for (unsigned step = 0; step <= 96; ++step) {
            const double time = step / 32.;
            const double a = std::clamp((time - .5) / 1.5, 0., 1.);
            const double b = std::clamp((time - 1.) / 1.5, 0., 1.);
            const std::array weights{(1 - a) * (1 - b), a * (1 - b), b};
            std::size_t strongest = 0;
            for (std::size_t i = 1; i < weights.size(); ++i)
                if (weights[i] >= weights[strongest])
                    strongest = i;
            const double speed = weights[0] * (20 + 10 * std::clamp(time, 0., 2.)) +
                                 weights[1] * (100 + 10 * std::clamp(time - .5, 0., 2.)) +
                                 weights[2] * (20 + 10 * std::clamp(time - 1., 0., 2.));
            AnimationPlaybackAccess::control(*mixed.animation, track.sequence,
                                             {{"operation", "seek"}, {"position_seconds", time}});
            verify(mixed.scene.entity(actor).get<Motion>(), speed, strongest == 1);
        }
        // At1.5s the middle clip is strongest (4/9), although the two other
        // clips together carry5/9. Discrete fields choose one clip, not a vote.
        AnimationPlaybackAccess::control(*mixed.animation, track.sequence,
                                         {{"operation", "seek"}, {"position_seconds", 1.5}});
        verify(mixed.scene.entity(actor).get<Motion>(), 65, true);
        const auto checkpoint = mixed.animation->checkpoint();
        Runtime cold(root, layered_source, catalog);
        cold.animation->restore(checkpoint);
        verify(cold.scene.entity(actor).get<Motion>(), 65, true);
        check(cold.animation->checkpoint() == checkpoint,
              "Multiway typed recovery changed exact Boolean/integer fields");
    }
    {
        auto collection = track;
        for (std::size_t i = 2; i < sequence_clip_limit; ++i)
            collection.sequence_extra_clips.push_back({{i % 2 ? next : first}, double(i)});
        auto longer = sequence;
        longer.duration = 10;
        scene.entity(controller).set(longer);
        scene.entity(actor).set(collection);
        const auto collection_source = scene.snapshot();
        scene.restore_snapshot(saved);
        Runtime many(root, collection_source, catalog);
        many.ready();
        for (const double time : {1.5, 2.25, 2.75, 3.5, 4.25, 5.5, 6.25, 7.499999, 7.5, 8.5}) {
            AnimationPlaybackAccess::control(*many.animation, track.sequence,
                                             {{"operation", "seek"}, {"position_seconds", time}});
            const auto active = std::clamp(int(std::floor(time)) - 1, 0, 6);
            const auto at = std::clamp(time - active, 0., 2.);
            const auto bt = std::clamp(time - active - 1, 0., 2.);
            const auto weight = std::clamp(time - active - 1, 0., 1.);
            const auto speed = std::lerp((active % 2 ? 100. : 20.) + 10 * at,
                                         (active % 2 ? 20. : 100.) + 10 * bt, weight);
            verify(many.scene.entity(actor).get<Motion>(), speed,
                   active % 2 ? weight < .5 : weight >= .5);
        }
        Runtime fixed_many(root, collection_source, catalog);
        fixed_many.ready();
        for (unsigned tick = 0; tick < 30; ++tick)
            fixed_many.simulation.tick(.25);
        verify(fixed_many.scene.entity(actor).get<Motion>(), 70, true);
        const auto copied = fixed_many.simulation.presentation(.5);
        bool found = false;
        for (const auto& row : copied.at("entities"))
            if (row.at("id") == actor) {
                const auto& value = row.at("components").at(codec.key());
                near(value.at("speed"), 60);
                check(value.at("enabled") == true &&
                          value.at("marker").get<std::int64_t>() == INT64_MIN &&
                          value.at("score").get<std::uint64_t>() == UINT64_MAX,
                      "Eight-clip copied presentation rounded an exact held primitive");
                found = true;
            }
        check(found, "Eight-clip presentation lost its reflected component");
    }
    live.simulation.tick(1.f);
    live.simulation.tick(.5f);
    verify(entity.get<Motion>(), 70, true);
    const auto native = codec.read(entity, true);
    const auto preview = live.simulation.presentation(.5);
    bool found = false;
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            const auto& value = row.at("components").at(codec.key());
            near(value.at("speed"), 50);
            check(value.at("enabled") == true &&
                      value.at("marker").get<std::int64_t>() == INT64_MIN &&
                      value.at("score").get<std::uint64_t>() == UINT64_MAX,
                  "Presentation blend rounded a held reflected primitive");
            found = true;
        }
    check(found && codec.read(entity, true) == native,
          "Sequence component presentation changed its native fields");
    const auto checkpoint = live.animation->checkpoint();
    Runtime recovered(root, live.scene.snapshot(), catalog);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    verify(recovered.scene.entity(actor).get<Motion>(), 70, true);
    const EntityRef ref{scene.asset_id(), EntityId::parse(controller)};
    for (const auto& [time, speed, incoming] :
         std::array{std::tuple{0., 20., false}, std::tuple{1.25, 50., false},
                    std::tuple{1.499999, 69.99992, false}, std::tuple{1.5, 70., true},
                    std::tuple{2., 110., true}, std::tuple{3.5, 120., true}}) {
        AnimationPlaybackAccess::control(*live.animation, ref,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        verify(entity.get<Motion>(), speed, incoming);
    }
    check(scene.snapshot() == saved, "Reflected pair playback changed its authored Scene");
    for (const auto invalid : {missing, range, stamp}) {
        track.sequence_next_clip = {invalid};
        scene.entity(actor).set(track);
        Runtime rejected(root, scene.snapshot(), catalog);
        const auto before = rejected.scene.snapshot();
        rejects([&] { rejected.ready(); });
        check(rejected.scene.snapshot() == before,
              "Rejected reflected pair changed native values before contribution");
    }
    TransformAnimationDocument replacement(lease, "Next.animation.json");
    replacement.edit(replacement.revision(), "Reorder incoming field bindings", [&](auto& source) {
        std::reverse(source["component_bindings"].begin(), source["component_bindings"].end());
        for (auto& key : source["keys"])
            std::reverse(key["component_values"].begin(), key["component_values"].end());
    });
    live.animation->catalog(publish(replacement));
    live.ready();
    AnimationPlaybackAccess::control(*live.animation, ref,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    verify(entity.get<Motion>(), 70, true);
    const auto before_restore = live.scene.snapshot();
    const auto before_restore_clock = live.animation->checkpoint();
    rejects([&] { live.animation->restore(checkpoint); });
    check(live.scene.snapshot() == before_restore &&
              live.animation->checkpoint() == before_restore_clock,
          "Recovery accepted old binding order revision or partially changed native values");
}
