#pragma once
#include <limits>
namespace {
void transform_curve_tests(const std::filesystem::path& root,
                           const std::shared_ptr<ProjectLease>& lease) {
    auto document = TransformAnimationDocument::create(lease, "Assets/Curves.animation.json");
    auto curves = document->source();
    const LocalTransform end{{4, 0, 0}, rotation_from_euler({0, 90, 0}), {3, 1, 1}};
    const auto finish = curves.set_key(2 * animation_timebase, end);
    const auto start = curves.set_key(0, {}, false, {}, TransformAnimationCurves{true});
    require(curves.document.at("version") == 2, "Curve source did not declare version2");
    const auto quarter = curves.clip().sample(500000);
    near(quarter.translation.x, .625);
    near(quarter.scale.x, 1.3125);
    near(quarter.rotation.y, rotation_from_euler({0, 22.5, 0}).y);
    near(curves.clip().sample(2000000).translation.x, 4);
    near(curves.clip().sample(-1).translation.x, 0);
    auto outgoing = TransformAnimationCurves{true};
    outgoing.outgoing.translation[1] = 4;
    auto incoming = TransformAnimationCurves{};
    incoming.incoming.translation[1] = -4;
    curves.set_key(2000000, end, false, finish, incoming);
    curves.set_key(0, {}, false, start, outgoing);
    near(curves.clip().sample(1000000).translation.y, 2);
    near(curves.clip().sample(500000).translation.y, 1.5);
    const auto decoded = decode_transform_animation(curves.cook(), curves.asset());
    near(decoded.sample(500000).translation.x, .625);
    near(decoded.sample(500000).translation.y, 1.5);
    auto legacy = document->source().document;
    legacy["format"] = "forge.transform-clip";
    const auto text = legacy.dump();
    near(decode_transform_animation(std::as_bytes(std::span(text)), document->source().asset())
             .sample(500000)
             .translation.x,
         0);
    auto preserved = curves;
    const auto metadata = preserved.document.at("keys")[1].at("curves");
    preserved.set_key(2000000, end, false, finish);
    require(preserved.document.at("keys")[1].at("curves") == metadata,
            "Updating pose erased curve slopes");
    preserved.move_key(finish, 1500000);
    require(preserved.document.at("keys")[1].at("curves") == metadata,
            "Moving key erased curve slopes");
    const auto stable = preserved.document;
    rejects([&] { preserved.move_key(finish, 0); });
    require(preserved.document == stable, "Colliding key move changed curve source");
    auto held = curves;
    held.set_key(0, {}, true, start);
    near(held.clip().sample(1999999).translation.x, 0);
    near(held.clip().sample(2000000).translation.x, 4);
    require(held.document.at("keys")[0].at("curves") == curves.document.at("keys")[0].at("curves"),
            "Hold erased preserved vector curves");
    auto straight = curves;
    outgoing = TransformAnimationCurves{true};
    outgoing.outgoing.translation[0] = 2;
    incoming = {};
    incoming.incoming.translation[0] = 2;
    straight.set_key(2000000, end, false, finish, incoming);
    straight.set_key(0, {}, false, start, outgoing);
    near(straight.clip().sample(500000).translation.x, 1);
    near(straight.clip().sample(10).translation.x / .00001, 2);
    auto signed_scale = curves;
    signed_scale.document["keys"][0]["scale"] = {-3, 0, -1};
    signed_scale.document["keys"][1]["scale"] = {3, 0, -1};
    signed_scale.validate();
    near(signed_scale.clip().sample(1000000).scale.x, 0);
    near(signed_scale.clip().sample(1000000).scale.y, 0);
    near(signed_scale.clip().sample(1000000).scale.z, -1);
    auto selective = curves;
    selective.document["channels"] = {"forge.local_scale"};
    near(selective.clip().sample(500000).translation.x, 1);
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), 1e10}) {
        auto safe = curves;
        auto bad = TransformAnimationCurves{true};
        bad.outgoing.translation[0] = invalid;
        rejects([&] { safe.set_key(0, {}, false, start, bad); });
        require(safe.document == curves.document, "Bad slope mutated source");
    }
    for (bool translation : {true, false}) {
        auto too_far = curves;
        const char* field = translation ? "translation" : "scale";
        const auto value = translation ? 9.9e8 : 9000.;
        const auto slope = translation ? 1e9 : 10000.;
        too_far.document["keys"][0][field][0] = value;
        too_far.document["keys"][1][field][0] = value;
        too_far.document["keys"][0]["curves"]["outgoing"][field][0] = slope;
        too_far.document["keys"][1]["curves"]["incoming"][field][0] = -slope;
        rejects([&] { too_far.validate(); });
    }
    auto wrong = curves;
    wrong.document["version"] = 1;
    rejects([&] { wrong.validate(); });
    wrong = curves;
    wrong.document["keys"][0]["curves"]["incoming"]["translation"] = {0, 0};
    rejects([&] { wrong.validate(); });
    auto full = curves;
    full.document["duration_us"] = 5000000;
    full.document["keys"] = Json::array();
    for (unsigned i = 0; i < 4096; ++i) {
        auto key = curves.document.at("keys")[0];
        key["id"] = AssetId::generate();
        key["time_us"] = std::int64_t(i) * 1000;
        full.document["keys"].push_back(key);
    }
    full.validate();
    const auto full_text = full.document.dump();
    require(full_text.size() <= transform_animation_byte_limit,
            "Full curve profile exceeds byte budget");
    const auto parsed = TransformAnimationSource::parse(std::as_bytes(std::span(full_text)));
    require(parsed.clip().keys.size() == 4096 &&
                decode_transform_animation(full.cook(), full.asset()).keys.size() == 4096,
            "Maximum curve key profile does not parse/cook");
    document->edit(document->revision(), "Vector curves",
                   [&](Json& draft) { draft = curves.document; });
    document->undo();
    legacy["format"] = "forge.transform-animation";
    require(document->source().document == legacy, "Curve Undo did not restore legacy source");
    document->redo();
    document->save();
    TransformAnimationDocument reopened(lease, document->locator());
    require(reopened.source().document == curves.document, "Curve save/reopen differs");
    AssetImportService service(lease, transform_animation_import_registry(),
                               {"linux", "none", "cpu"});
    require(publish(service, document->locator(), curves.asset()).published,
            "Curve publication failed");
    const auto catalog = AssetCatalog::open_project(root);
    require(catalog.records().at(curves.asset()).metadata.at("forge.import").at("output_version") ==
                5,
            "Curve publication retained old recipe version");
    EngineContext author;
    Scene scene(author.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Curved cube"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.transform_animator"}});
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.transform_animator"},
                       {"field", "clip"},
                       {"value", curves.asset()}});
    const auto authored = scene.document();
    Fixture live(root);
    live.load(authored);
    live.ready();
    live.simulation.tick(.5f);
    near(live.scene.entity(actor).get<LocalTranslation>().x, .625);
    near(live.scene.entity(actor).get<LocalTranslation>().y, 1.5);
    near(live.scene.entity(actor).get<LocalScale>().x, 1.3125);
    const auto checkpoint = live.animation->checkpoint();
    Fixture recovered(root);
    recovered.load(authored);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    near(recovered.scene.entity(actor).get<LocalTranslation>().y, 1.5);
    recovered.simulation.tick(.5f);
    near(recovered.scene.entity(actor).get<LocalTranslation>().x, 2);
    near(recovered.scene.entity(actor).get<LocalTranslation>().y, 2);
    auto config = live.scene.entity(actor).get<TransformAnimator>();
    config.playback_speed = 0;
    live.scene.entity(actor).set(config);
    live.simulation.tick(.5f);
    near(live.scene.entity(actor).get<LocalTranslation>().x, .625);
    config.playback_speed = 1;
    config.loop = false;
    live.scene.entity(actor).set(config);
    live.simulation.tick(2.5f);
    near(live.scene.entity(actor).get<LocalTranslation>().x, 4);
    near(live.scene.entity(actor).get<LocalTranslation>().y, 0);
    atomic_write(root / "curve.scene.json", authored.dump(2));
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    const auto package = root.parent_path() / ("curve-export-" + AssetId::generate().str());
    (void)package_runtime_content(root, package, roots, {"linux", "none"});
    require(!std::filesystem::exists(package / document->locator()),
            "Curve source leaked into export");
    Fixture exported(package);
    exported.load(authored);
    exported.ready();
    exported.simulation.tick(.5f);
    near(exported.scene.entity(actor).get<LocalTranslation>().x, .625);
    near(exported.scene.entity(actor).get<LocalTranslation>().y, 1.5);
    const auto prior = AssetCatalog::open_project(root).document();
    atomic_write(root / document->locator(), "{ invalid curves");
    bool failed = false;
    try {
        failed = !publish(service, document->locator(), curves.asset()).published;
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid curve source published successfully");
    require(AssetCatalog::open_project(root).document() == prior,
            "Bad curve import replaced last good selection");
    std::cout << "Cubic vector analytic/slope/bounds/legacy/cook/history/runtime/recovery/export "
                 "passed\n";
}
} // namespace
