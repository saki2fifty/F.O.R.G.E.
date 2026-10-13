#pragma once
namespace {
void animation_tangent_preset_tests() {
    using Preset = TransformAnimationTangentPreset;
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.document["duration_us"] = 4 * animation_timebase;
    const auto first = source.clip().keys[0].id;
    const auto middle = source.set_key(animation_timebase,
                                       {{2, 0, 1000}, rotation_from_euler({0, 90, 0}), {3, 1, 1}});
    const auto last = source.set_key(4 * animation_timebase,
                                     {{5, 9, 1001}, rotation_from_euler({0, 180, 0}), {0, 1, 1}});
    source.document["keys"][1]["extension"] = AssetId::generate();
    source.document["keys"][1]["interpolation"] = "held";
    const std::array keys{first, middle, last};
    auto smooth = source;
    smooth.set_tangent_preset(keys, Preset::Smooth);
    require(smooth.document.at("version") == 2, "Tangent presets did not declare curve version2");
    const auto compiled = smooth.clip();
    near(compiled.keys[1].curves.incoming.translation[0], 1.75);
    near(compiled.keys[1].curves.outgoing.translation[0], 1.75);
    near(compiled.keys[1].curves.incoming.translation[1], 0);
    near(compiled.keys[1].curves.incoming.translation[2], 1);
    near(compiled.keys[1].curves.incoming.scale[0], 0);
    near(compiled.keys[0].curves.incoming.translation[0], 2);
    near(compiled.keys[2].curves.outgoing.translation[0], 1);
    require(compiled.keys[1].held &&
                smooth.document["keys"][1]["extension"] == source.document["keys"][1]["extension"],
            "Tangent preset erased Hold or opaque key metadata");
    for (unsigned i = 0; i < keys.size(); ++i) {
        require(compiled.keys[i].id == keys[i] &&
                    compiled.keys[i].time == source.clip().keys[i].time,
                "Tangent preset changed key identity or time");
        require(smooth.document["keys"][i]["rotation"] == source.document["keys"][i]["rotation"],
                "Vector tangents changed quaternion key values");
    }
    auto linear = source;
    linear.set_tangent_preset(std::array{middle}, Preset::Linear);
    near(linear.clip().keys[1].curves.incoming.translation[0], 2);
    near(linear.clip().keys[1].curves.outgoing.translation[0], 1);
    require(linear.document["keys"][0] == source.document["keys"][0] &&
                linear.document["keys"][2] == source.document["keys"][2],
            "Selected tangent preset changed unrelated keys");
    auto flat = smooth;
    flat.set_tangent_preset(keys, Preset::Flat);
    for (const auto& key : flat.clip().keys)
        for (unsigned axis = 0; axis < 3; ++axis) {
            near(key.curves.incoming.translation[axis], 0);
            near(key.curves.outgoing.translation[axis], 0);
            near(key.curves.incoming.scale[axis], 0);
            near(key.curves.outgoing.scale[axis], 0);
        }
    auto extended = smooth;
    auto& row = extended.document["keys"][1];
    row["curves"]["custom"] = "retain root";
    row["curves"]["incoming"]["custom"] = "retain side";
    const auto opaque = row["extension"];
    const auto pose = extended.clip().keys[1].pose;
    const auto curve = extended.clip().keys[1].curves;
    extended.set_key(animation_timebase, pose, true, middle, curve);
    require(extended.document["keys"][1]["extension"] == opaque &&
                extended.document["keys"][1]["curves"]["custom"] == "retain root" &&
                extended.document["keys"][1]["curves"]["incoming"]["custom"] == "retain side",
            "Curve/pose update erased opaque source extensions");
    extended.set_tangent_preset(keys, Preset::Linear);
    require(extended.document["keys"][1]["curves"]["custom"] == "retain root" &&
                extended.document["keys"][1]["curves"]["incoming"]["custom"] == "retain side",
            "Preset erased opaque curve extensions");
    const auto stable = extended.document;
    for (const auto selected : {std::vector<AssetId>{}, std::vector<AssetId>{first, first},
                                std::vector<AssetId>{AssetId::generate()}}) {
        rejects([&] { extended.set_tangent_preset(selected, Preset::Smooth); });
        require(extended.document == stable, "Rejected preset selection changed source");
    }
    rejects([&] { extended.set_tangent_preset(keys, static_cast<Preset>(999)); });
    require(extended.document == stable, "Unknown preset changed source");
    auto excessive = TransformAnimationSource::create(AssetId::generate());
    const auto end = excessive.set_key(1, {{2000, 0, 0}, {}, {1, 1, 1}});
    const auto excessive_stable = excessive.document;
    rejects([&] { excessive.set_tangent_preset(std::array{end}, Preset::Linear); });
    require(excessive.document == excessive_stable,
            "Excessive automatic slope partially committed");
    auto lone = TransformAnimationSource::create(AssetId::generate());
    lone.set_tangent_preset(std::array{lone.clip().keys[0].id}, Preset::Smooth);
    near(lone.clip().keys[0].curves.incoming.translation[0], 0);
    auto straight = TransformAnimationSource::create(AssetId::generate());
    const auto start = straight.clip().keys[0].id;
    const auto finish = straight.set_key(2 * animation_timebase, {{4, 0, 0}, {}, {3, 1, 1}});
    straight.set_tangent_preset(std::array{start, finish}, Preset::Linear);
    const auto decoded = decode_transform_animation(straight.cook(), straight.asset());
    near(decoded.sample(500000).translation.x, 1);
    near(decoded.sample(500000).scale.x, 1.5);
}
void animation_tangent_preset_pipeline_tests(const std::filesystem::path& root,
                                             const std::shared_ptr<ProjectLease>& lease) {
    auto document = TransformAnimationDocument::create(lease, "Assets/PresetMotion.animation.json");
    const auto original = document->source().document;
    document->edit(document->revision(), "Linear motion preset", [](Json& draft) {
        TransformAnimationSource source{draft};
        const auto first = source.clip().keys[0].id;
        const auto last = source.set_key(2 * animation_timebase, {{4, 0, 0}, {}, {3, 1, 1}});
        source.set_tangent_preset(std::array{first, last}, TransformAnimationTangentPreset::Linear);
        draft = std::move(source.document);
    });
    const auto applied = document->source().document;
    document->undo();
    require(document->source().document == original, "Preset pipeline Undo failed");
    document->redo();
    require(document->source().document == applied, "Preset pipeline Redo changed key identities");
    document->save();
    require(TransformAnimationDocument(lease, document->locator()).source().document == applied,
            "Preset pipeline reopen lost keys/slopes");
    AssetImportService service(lease, transform_animation_import_registry(),
                               {"linux", "none", "cpu"});
    require(publish(service, document->locator(), document->source().asset()).published,
            "Preset publication failed");
    EngineContext author;
    Scene scene(author.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Preset cube"}})
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
    auto preview = authored;
    require(apply_transform_animation_preview(preview, {scene.asset_id(), EntityId::parse(actor)},
                                              document->source().clip(), 500000),
            "Preset preview target was not resolved");
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            near(row.at("components").at("forge.local_translation").at("x"), 1);
            near(row.at("components").at("forge.local_scale").at("x"), 1.5);
        }
    require(scene.document() == authored, "Preset preview edited authored Scene");
    Fixture live(root);
    live.load(authored);
    live.ready();
    live.simulation.tick(.5f);
    near(live.scene.entity(actor).get<LocalTranslation>().x, 1);
    near(live.scene.entity(actor).get<LocalScale>().x, 1.5);
    atomic_write(root / "preset.scene.json", authored.dump(2));
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto output = root.parent_path() / ("preset-export-" + AssetId::generate().str());
    package_runtime_content(root, output, roots, {"linux", "none"});
    const auto relocated = root.parent_path() / ("preset-relocated-" + AssetId::generate().str());
    std::filesystem::rename(output, relocated);
    require(!std::filesystem::exists(relocated / document->locator()),
            "Preset source leaked into export");
    Fixture exported(relocated);
    exported.load(authored);
    exported.ready();
    exported.simulation.tick(.5f);
    near(exported.scene.entity(actor).get<LocalTranslation>().x, 1);
    near(exported.scene.entity(actor).get<LocalScale>().x, 1.5);
    std::cout
        << "Tangent preset source/history/preview/published runtime and relocated export match\n";
}
} // namespace
