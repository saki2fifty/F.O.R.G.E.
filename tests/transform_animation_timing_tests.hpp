#pragma once
namespace {
void animation_timing_tests() {
    auto source = TransformAnimationSource::create(AssetId::generate());
    const auto first = source.clip().keys.front().id;
    TransformAnimationCurves curve;
    curve.cubic = true;
    curve.incoming.translation = curve.outgoing.translation = {2, 1, 0};
    curve.incoming.scale = curve.outgoing.scale = {.5, 0, 0};
    source.set_key(0, {}, false, first, curve);
    source.set_key(animation_timebase, {{2, 1, 0}, rotation_from_euler({0, 90, 0}), {2, 1, 1}},
                   true, {}, curve);
    source.set_key(2 * animation_timebase, {{4, 0, 0}, rotation_from_euler({0, 180, 0}), {1, 2, 1}},
                   false, {}, curve);
    source.document["opaque"] = {{"time_us", 123}};
    for (auto& key : source.document["keys"]) {
        key["opaque"] = {{"keep", 73}};
        key["curves"]["opaque"] = true;
        key["curves"]["incoming"]["opaque"] = {1, 2};
        key["curves"]["outgoing"]["opaque"] = "keep";
    }
    source.validate();
    const auto original = source.document;
    auto expected = original;
    expected["duration_us"] = 4 * animation_timebase;
    for (auto& key : expected["keys"]) {
        key["time_us"] = key["time_us"].get<std::int64_t>() * 2;
        for (const char* side : {"incoming", "outgoing"})
            for (const char* channel : {"translation", "scale"})
                for (auto& value : key["curves"][side][channel])
                    value = value.get<double>() * .5;
    }
    source.retime_duration(4 * animation_timebase);
    require(source.document == expected, "Retime changed pose/IDs/held/opaque fields or slopes");
    const auto prior = TransformAnimationSource{original}.clip(), retimed = source.clip();
    const auto cooked = decode_transform_animation(source.cook(), source.asset());
    for (unsigned step = 0; step <= 200; ++step) {
        const double phase = double(step) / 200;
        const auto a = prior.sample(phase * prior.duration),
                   b = retimed.sample(phase * retimed.duration),
                   c = cooked.sample(phase * retimed.duration);
        near(a.translation.x, b.translation.x);
        near(a.translation.y, b.translation.y);
        near(a.scale.x, b.scale.x);
        near(a.scale.y, b.scale.y);
        near(std::abs(a.rotation.y), std::abs(b.rotation.y));
        near(std::abs(a.rotation.w), std::abs(b.rotation.w));
        near(b.translation.x, c.translation.x);
        near(b.scale.x, c.scale.x);
    }
    const auto once = source.document;
    source.retime_duration(source.clip().duration);
    require(source.document == once, "Retime no-op changed source");
    auto reject_retime = [&](std::int64_t duration) {
        const auto before = source.document;
        rejects([&] { source.retime_duration(duration); });
        require(source.document == before, "Rejected retime partially committed");
    };
    reject_retime(0);
    reject_retime(-1);
    reject_retime(3600 * animation_timebase + 1);
    reject_retime(1); // Three distinct times collapse onto two microseconds.
    auto small = TransformAnimationSource::create(AssetId::generate());
    small.document["duration_us"] = 2;
    small.set_key(1, {});
    small.set_key(2, {});
    small.retime_duration(3);
    require(small.clip().keys[1].time == 2 && small.clip().keys.back().time == 3,
            "Retime half-microsecond rounding differs");
    auto large = TransformAnimationSource::create(AssetId::generate());
    large.document["duration_us"] = 3600 * animation_timebase;
    large.set_key(3600 * animation_timebase, {});
    large.retime_duration(3600 * animation_timebase - 1);
    require(large.clip().keys.back().time == 3600 * animation_timebase - 1,
            "Maximum duration unsigned product overflowed");
    auto steep = TransformAnimationSource::create(AssetId::generate());
    curve.incoming.translation = curve.outgoing.translation = {1e9, 0, 0};
    steep.set_key(0, {}, false, steep.clip().keys.front().id, curve);
    steep.set_key(2 * animation_timebase, {}, false, {}, curve);
    const auto steep_before = steep.document;
    rejects([&] { steep.retime_duration(animation_timebase); });
    require(steep.document == steep_before, "Invalid rescaled slopes partially committed");

    auto grid = TransformAnimationSource::create(AssetId::generate());
    grid.document["duration_us"] = animation_timebase;
    const auto a = grid.set_key(290000, {{1, 0, 0}, {}, {1, 1, 1}}),
               b = grid.set_key(710000, {{2, 0, 0}, {}, {1, 1, 1}});
    grid.document["keys"][1]["opaque"] = {{"keep", true}};
    const auto grid_before = grid.document;
    grid.snap_keys_to_frames(std::array{a, b}, 4);
    auto reverse_order = TransformAnimationSource{grid_before};
    reverse_order.snap_keys_to_frames(std::array{b, a}, 4);
    require(grid.document == reverse_order.document, "Selection order changed frame snapping");
    expected = grid_before;
    expected["keys"][1]["time_us"] = 250000;
    expected["keys"][2]["time_us"] = 750000;
    require(grid.document == expected, "Frame snapping changed non-time fields or wrong keys");
    grid.snap_keys_to_frames(std::array{a, b}, 4);
    require(grid.document == expected, "Aligned frame grid changed source");
    auto reject_grid = [&](auto ids, unsigned fps) {
        const auto before = grid.document;
        rejects([&] { grid.snap_keys_to_frames(ids, fps); });
        require(grid.document == before, "Rejected frame snapping partially committed");
    };
    reject_grid(std::array{a, b}, 0);
    reject_grid(std::array{a, b}, 241);
    reject_grid(std::array{a, a}, 30);
    reject_grid(std::array{AssetId::generate()}, 30);
    reject_grid(std::array<AssetId, 0>{}, 30);
    reject_grid(std::array{a}, 1); // Collides with retained key at zero.
    for (const auto [time, fps, result] :
         {std::array<std::int64_t, 3>{250000, 2, 500000}, std::array<std::int64_t, 3>{20833, 24, 0},
          std::array<std::int64_t, 3>{20834, 24, 41667}, std::array<std::int64_t, 3>{2083, 240, 0},
          std::array<std::int64_t, 3>{2084, 240, 4167}}) {
        auto lone = TransformAnimationSource::create(AssetId::generate());
        lone.document["keys"][0]["time_us"] = time;
        const auto id = lone.clip().keys[0].id;
        lone.snap_keys_to_frames(std::array{id}, unsigned(fps));
        require(lone.clip().keys[0].time == result, "Frame rational/tie rounding differs");
    }
    auto end = TransformAnimationSource::create(AssetId::generate());
    end.document["duration_us"] = 900000;
    end.document["keys"][0]["time_us"] = 890000;
    const auto end_before = end.document;
    rejects([&] { end.snap_keys_to_frames(std::array{end.clip().keys[0].id}, 2); });
    require(end.document == end_before, "Beyond-duration frame snap partially committed");

    // Moving .1/.9s to 0/1s lengthens this cubic segment. Its original maximum
    // is 980 million; unchanged slopes would exceed the 1e9 admitted bound.
    auto overshoot = TransformAnimationSource::create(AssetId::generate());
    overshoot.document["duration_us"] = animation_timebase;
    TransformAnimationCurves bounded;
    bounded.cubic = true;
    bounded.incoming.translation = {-1e9, 0, 0};
    bounded.outgoing.translation = {1e9, 0, 0};
    const auto initial = overshoot.clip().keys[0].id;
    const LocalTransform high{{780000000, 0, 0}, {}, {1, 1, 1}};
    overshoot.set_key(0, high, false, initial, bounded);
    overshoot.move_key(initial, 100000);
    const auto final = overshoot.set_key(900000, high, false, {}, bounded);
    near(overshoot.clip().sample(500000).translation.x, 980000000);
    const auto overshoot_before = overshoot.document;
    rejects([&] { overshoot.snap_keys_to_frames(std::array{initial, final}, 1); });
    require(overshoot.document == overshoot_before, "Invalid snapped cubic partially committed");

    auto max = TransformAnimationSource::create(AssetId::generate());
    max.document["duration_us"] = 3600 * animation_timebase;
    const auto template_key = max.document["keys"][0];
    max.document["keys"] = Json::array();
    std::vector<AssetId> selected;
    for (unsigned i = 0; i < 4096; ++i) {
        auto key = template_key;
        const auto id = AssetId::generate();
        key["id"] = id;
        key["time_us"] = std::int64_t(i) * 500000;
        key["opaque"] = {{"keep", i}};
        max.document["keys"].push_back(std::move(key));
        selected.push_back(id);
    }
    max.validate();
    max.retime_duration(1800 * animation_timebase);
    const auto max_before = max.document;
    max.snap_keys_to_frames(selected, 4);
    require(max.document == max_before && max.clip().keys.size() == 4096,
            "Maximum-profile timing lost aligned key metadata");
}
void animation_timing_pipeline_tests(const std::filesystem::path& root,
                                     const std::shared_ptr<ProjectLease>& lease) {
    for (bool retime : {true, false}) {
        const auto name = retime ? "Retimed" : "FrameGrid";
        auto document = TransformAnimationDocument::create(
            lease, std::filesystem::path("Assets") / (std::string(name) + ".animation.json"));
        document->edit(document->revision(), "Authored timing fixture", [&](Json& draft) {
            TransformAnimationSource source{draft};
            if (retime) {
                const auto first = source.clip().keys.front().id;
                const auto last =
                    source.set_key(2 * animation_timebase, {{4, 0, 0}, {}, {3, 1, 1}});
                source.set_tangent_preset(std::array{first, last},
                                          TransformAnimationTangentPreset::Linear);
            } else {
                source.document["duration_us"] = animation_timebase;
                source.set_key(270000, {{1, 0, 0}, {}, {1.5, 1, 1}});
                source.set_key(760000, {{3, 0, 0}, {}, {2.5, 1, 1}});
                source.set_key(animation_timebase, {{4, 0, 0}, {}, {3, 1, 1}});
            }
            draft = std::move(source.document);
        });
        const auto before = document->source().document;
        const auto revision = document->revision();
        document->edit(revision, "Apply timing", [&](Json& draft) {
            TransformAnimationSource source{draft};
            if (retime)
                source.retime_duration(4 * animation_timebase);
            else {
                std::vector<AssetId> keys;
                for (const auto& key : source.clip().keys)
                    keys.push_back(key.id);
                source.snap_keys_to_frames(keys, 4);
            }
            draft = std::move(source.document);
        });
        const auto applied = document->source().document;
        require(document->revision() == revision + 1, "Timing edit is not one history step");
        document->undo();
        require(document->source().document == before, "Timing Undo differs");
        document->redo();
        require(document->source().document == applied, "Timing Redo changed source/identities");
        document->save();
        require(TransformAnimationDocument(lease, document->locator()).source().document == applied,
                "Timing save/reopen changed source");
        AssetImportService service(lease, transform_animation_import_registry(),
                                   {"linux", "none", "cpu"});
        require(publish(service, document->locator(), document->source().asset()).published,
                "Timing publication failed");
        EngineContext author;
        Scene scene(author.world());
        const auto actor = authoring_command(scene, "entity.create", {{"name", name}})
                               .at("selected")
                               .get<std::string>();
        authoring_command(scene, "component.add",
                          {{"entity", actor}, {"component", "forge.transform_animator"}});
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.transform_animator"},
                           {"field", "clip"},
                           {"value", document->source().asset()}});
        const auto authored = scene.document();
        const double x = retime ? .5 : 2, scale = retime ? 1.25 : 2;
        auto preview = authored;
        require(apply_transform_animation_preview(preview,
                                                  {scene.asset_id(), EntityId::parse(actor)},
                                                  document->source().clip(), 500000),
                "Timing preview target missing");
        for (const auto& row : preview["entities"])
            if (row["id"] == actor) {
                near(row["components"]["forge.local_translation"]["x"], x);
                near(row["components"]["forge.local_scale"]["x"], scale);
            }
        require(scene.document() == authored, "Timing preview changed authored Scene");
        Fixture live(root);
        live.load(authored);
        live.ready();
        live.simulation.tick(.5f);
        near(live.scene.entity(actor).get<LocalTranslation>().x, x);
        near(live.scene.entity(actor).get<LocalScale>().x, scale);
        atomic_write(root / (std::string(name) + ".scene.json"), authored.dump(2));
        const std::array roots{scene.asset_id()};
        prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
        const auto output =
            root.parent_path() / (std::string(name) + "-export-" + AssetId::generate().str());
        package_runtime_content(root, output, roots, {"linux", "none"});
        const auto relocated =
            root.parent_path() / (std::string(name) + "-relocated-" + AssetId::generate().str());
        std::filesystem::rename(output, relocated);
        require(!std::filesystem::exists(relocated / document->locator()),
                "Timing source leaked into export");
        Fixture exported(relocated);
        exported.load(authored);
        exported.ready();
        exported.simulation.tick(.5f);
        near(exported.scene.entity(actor).get<LocalTranslation>().x, x);
        near(exported.scene.entity(actor).get<LocalScale>().x, scale);
    }
    std::cout << "Timing history/reopen/publication/preview/runtime and relocated source-free "
                 "export match\n";
}
} // namespace
