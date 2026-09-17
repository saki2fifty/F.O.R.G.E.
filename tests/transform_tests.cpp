#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/geometry.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace forge;
namespace {
void check(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void expect_near(double a, double b, double tolerance = 2e-6) {
    if (std::abs(a - b) > tolerance)
        throw std::runtime_error("Numeric mismatch: " + std::to_string(a) + " vs " +
                                 std::to_string(b));
}
Json xyz(double x, double y = 0, double z = 0) { return {{"x", x}, {"y", y}, {"z", z}}; }
Json row(const Json& doc, const std::string& id) {
    for (const auto& e : doc.at("entities"))
        if (e.at("id") == id)
            return e;
    throw std::runtime_error("Missing test row");
}
AffineTransform world(const Scene& s, const std::string& id) {
    auto e = row(s.effective_document(), s.canonical_id(id));
    check(e.at("spatial_resolved"), "Unresolved transform");
    return {e.at("world_affine").get<std::array<double, 12>>()};
}
void expect_near(AffineTransform a, AffineTransform b, double tolerance = 2e-6) {
    for (unsigned i = 0; i < 12; ++i)
        expect_near(a.m[i], b.m[i], tolerance);
}
std::string create(Scene& s, double x) {
    return authoring_command(s, "entity.create", {{"position", xyz(x)}}).at("selected");
}
Json legacy() {
    return {
        {"version", 1},
        {"plugin", {{"opaque", true}}},
        {"entities",
         Json::array({{{"id", "base"},
                       {"name", "Base"},
                       {"prefab", true},
                       {"components",
                        {{"forge.position", xyz(10)},
                         {"forge.rotation", xyz(30, 40, 50)},
                         {"forge.scale", xyz(2, 3, 4)}}}},
                      {{"id", "instance"},
                       {"name", "Instance"},
                       {"base", "base"},
                       {"components",
                        {{"forge.position", xyz(15)}, {"plugin.unknown", {{"entity", "base"}}}}}},
                      {{"id", "parent"},
                       {"name", "Parent"},
                       {"components", {{"forge.position", xyz(100)}}}},
                      {{"id", "child"},
                       {"name", "Child"},
                       {"parent", "parent"},
                       {"components", {{"forge.position", xyz(105)}}}}})}};
}
void math() {
    for (auto angles : {Double3{0, 0, 0},
                        {90, 0, 0},
                        {0, 90, 0},
                        {0, 0, 90},
                        {32, 57, 123},
                        {179.99, -179.99, 180},
                        {0, 90, 45},
                        {0, -90, 45}}) {
        const Json e = {{"components",
                         {{"forge.position", xyz(0)},
                          {"forge.rotation", xyz(angles[0], angles[1], angles[2])}}}};
        ObjectTransform old(e);
        auto q = rotation_from_euler(angles);
        auto m = affine_transform({{}, q, {}});
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = 0; j < 3; ++j)
                expect_near(m.m[4 * j + i], old.axes[i][j], 4e-7);
        expect_near(affine_transform({{}, rotation_from_euler(rotation_to_euler(q)), {}}), m, 5e-7);
    }
    LocalTransform t{{1e10 + .25, 2, 3}, rotation_from_euler({27, -89, 177}), {2, 3, 4}};
    auto m = affine_transform(t);
    expect_near(inverse(m) * m, AffineTransform{}, 5e-6);
    expect_near(affine_transform(decompose(m)), m, 2e-6);
    auto shear = affine_transform({{}, rotation_from_euler({0, 0, 25}), {2, 3, 1}}) *
                 affine_transform({{}, rotation_from_euler({0, 45, 15}), {}});
    rejects([&] { decompose(shear); });
    AffineTransform singular;
    singular.m[0] = 0;
    rejects([&] { inverse(singular); });
    rejects([] { normalized({0, 0, 0, 0}); });
    rejects([] { affine_transform({{}, {}, {-1, 1, 1}}); });
    std::map<std::uint64_t, TransformNode> deep;
    for (std::uint64_t i = 1; i <= 20000; ++i)
        deep.emplace(i, TransformNode{{{1, 0, 0}, {}, {}}, i - 1, true});
    auto result = evaluate_transforms(deep);
    expect_near(result.at(20000).affine.m[3], 20000);
    deep.at(1).parent = 20000;
    rejects([&] { evaluate_transforms(deep); });
}
void migration_and_inheritance() {
    EngineContext engine;
    Scene s(engine.world());
    auto old = legacy();
    old["entities"][0]["components"]["forge.rotation"]["unknown"] = "retain";
    s.reset(old);
    auto doc = s.document();
    check(doc.at("version") == 3, "No v3 migration");
    auto i = s.entity("instance"), b = s.entity("base");
    check(i.owns<LocalTranslation>() && !i.owns<LocalRotation>() && !i.owns<LocalScale>(),
          "Migration materialized inherited channels");
    check(!doc["entities"][1]["components"].contains("forge.local_rotation"),
          "Inherited rotation serialized");
    check(doc["entities"][0]["components"]["forge.local_rotation"]["legacy_euler_fields"]
             ["unknown"] == "retain",
          "Rotation opaque fields lost");
    expect_near(world(s, "child").m[3], 105);
    s.entity("parent").set<LocalTranslation>({200, 0, 0});
    expect_near(world(s, "child").m[3], 105);
    b.set<LocalScale>({5, 6, 7});
    b.set<LocalRotation>(rotation_from_euler({10, 20, 30}));
    expect_near(i.get<LocalScale>().x, 5);
    check(equivalent(i.get<LocalRotation>(), b.get<LocalRotation>()),
          "Prefab rotation update lost");
    auto view = s.effective_document();
    check(i.owns<WorldTransform>() && b.owns<WorldTransform>(),
          "World transforms not instance owned");
    const auto revision = s.revision(), derived = i.get<WorldTransform>().revision;
    s.effective_document();
    check(s.revision() == revision && i.get<WorldTransform>().revision == derived,
          "Unchanged derived read invalidated scene");
    authoring_command(s, "transform.world_translation",
                      {{"entity", s.canonical_id("instance")}, {"value", xyz(25)}});
    check(!i.owns<LocalRotation>() && !i.owns<LocalScale>(), "Move created unrelated overrides");
    authoring_command(s, "transform.local",
                      {{"entity", s.canonical_id("instance")},
                       {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}}});
    check(i.owns<LocalRotation>() && !i.owns<LocalScale>(), "Rotation setter owned scale");
    authoring_command(
        s, "component.revert",
        {{"entity", s.canonical_id("instance")}, {"component", "forge.local_rotation"}});
    check(!i.owns<LocalRotation>(), "Rotation revert failed");
    authoring_command(s, "transform.scale",
                      {{"entity", s.canonical_id("instance")}, {"value", xyz(3, 3, 3)}});
    check(i.owns<LocalScale>() && !i.owns<LocalRotation>(), "Scale setter owned rotation");
    authoring_command(s, "component.revert",
                      {{"entity", s.canonical_id("instance")}, {"component", "forge.local_scale"}});
    authoring_command(
        s, "component.revert",
        {{"entity", s.canonical_id("instance")}, {"component", "forge.local_translation"}});
    check(!i.owns<LocalTranslation>() && !i.owns<LocalScale>(), "Channel revert lost inheritance");
    auto before = s.document();
    rejects(
        [&] { authoring_command(s, "transform.local", {{"entity", s.canonical_id("instance")}}); });
    check(s.document() == before, "Empty write changed scene");
    auto bad = before;
    bad["entities"][1]["components"]["forge.world_transform"] = {{"x", 1}};
    rejects([&] { s.edit(bad); });
    check(s.document() == before, "Derived write committed");
    s.reset(before);
    check(s.document() == before, "Canonical quaternion roundtrip drift");
}
void hierarchy() {
    EngineContext engine;
    Scene s(engine.world());
    auto p = create(s, 10), c = create(s, 15), other = create(s, 99);
    auto handle = s.entity(c).id();
    s.reparent_entity(c, p);
    expect_near(s.entity(c).get<LocalTranslation>().x, 5);
    expect_near(world(s, c).m[3], 15);
    s.entity(p).set<LocalTranslation>({20, 0, 0});
    expect_near(world(s, c).m[3], 25);
    expect_near(world(s, other).m[3], 99);
    const auto unchanged = s.entity(other).get<WorldTransform>().revision;
    s.entity(p).set<LocalRotation>(rotation_from_euler({0, 0, 90}));
    auto point = world(s, c).point({0, 0, 0});
    expect_near(point[0], 20);
    expect_near(point[1], 5);
    check(s.entity(other).get<WorldTransform>().revision == unchanged,
          "Unrelated derived value rewritten");
    auto before = world(s, c);
    s.reparent_entity(c, "");
    expect_near(world(s, c), before);
    check(s.entity(c).id() == handle, "Reparent changed entity handle");
    s.reparent_entity(c, p, ReparentMode::KeepLocal);
    auto local = s.entity(c).get<LocalTranslation>();
    s.reparent_entity(c, "", ReparentMode::KeepLocal);
    check(s.entity(c).get<LocalTranslation>() == local, "Keep local changed channel");
    s.reparent_entity(c, p);
    auto doc = s.document();
    auto copy = s.duplicate_subtree(p);
    expect_near(world(s, copy), world(s, p));
    check(copy != p, "Duplicate identity reused");
    auto copied = s.document();
    s.undo();
    check(s.document() == doc, "Duplicate undo");
    s.redo();
    check(s.document() == copied, "Duplicate redo");
    authoring_command(
        s, "transform.binding",
        {{"entity", other}, {"spatial", {{"mode", "explicit"}, {"target", s.reference(p)}}}});
    auto other_world = world(s, other);
    s.entity(p).set<LocalTranslation>({30, 0, 0});
    expect_near(world(s, other).m[3], other_world.m[3] + 10);
    const auto saved = s.document();
    rejects([&] {
        authoring_command(
            s, "transform.binding",
            {{"entity", p}, {"spatial", {{"mode", "explicit"}, {"target", s.reference(other)}}}});
    });
    check(saved == s.document(), "Spatial cycle partially committed");
    auto retained = world(s, other);
    s.delete_subtree(p);
    expect_near(world(s, other), retained);
    check(s.entity(other).get<SpatialBinding>().mode == SpatialMode::World,
          "Explicit dependent not detached");
    s.undo();
    check(s.document() == saved, "Delete undo did not restore binding");
    const auto missing = EntityRef{s.asset_id(), EntityId::generate()};
    authoring_command(s, "transform.binding",
                      {{"entity", other},
                       {"mode", "keep_local"},
                       {"spatial", {{"mode", "explicit"}, {"target", missing}}}});
    check(!row(s.effective_document(), other).at("spatial_resolved").get<bool>(),
          "Missing parent silently became root");
    rejects([&] {
        authoring_command(s, "transform.world_translation", {{"entity", other}, {"value", xyz(0)}});
    });
}
void compensation() {
    EngineContext engine;
    Scene s(engine.world());
    s.reset(legacy());
    auto i = s.canonical_id("instance"), p = s.canonical_id("parent");
    s.reparent_entity(i, p);
    auto instance = s.entity(i);
    check(instance.owns<LocalTranslation>() && !instance.owns<LocalRotation>() &&
              !instance.owns<LocalScale>(),
          "Translation-only compensation owned unrelated channels");
    s.reparent_entity(i, "");
    s.entity(p).set<LocalScale>({2, 2, 2});
    s.entity(p).set<LocalRotation>(rotation_from_euler({0, 0, 45}));
    auto original = world(s, i);
    s.reparent_entity(i, p);
    expect_near(world(s, i), original, 4e-6);
    check(instance.owns<LocalRotation>() && instance.owns<LocalScale>(),
          "Necessary compensation overrides missing");
    auto a = create(s, 0), b = create(s, 5);
    s.entity(a).set<LocalScale>({2, 1, 1});
    s.entity(b).set<LocalRotation>(rotation_from_euler({0, 0, 45}));
    auto before = s.document();
    auto revision = s.revision();
    rejects([&] { s.reparent_entity(b, a); });
    check(s.document() == before && s.revision() == revision,
          "Shear rejection changed authored state");
    s.reparent_entity(b, a, ReparentMode::KeepLocal);
    auto shear = world(s, b);
    rejects([&] { s.reparent_entity(b, ""); });
    expect_near(world(s, b), shear);
    s.translate(1, 2, 3);
    auto translated = world(s, b);
    expect_near(translated.m[3], shear.m[3] + 1);
    expect_near(translated.m[7], shear.m[7] + 2);
    expect_near(translated.m[11], shear.m[11] + 3);
}
void generated_and_channels() {
    EngineContext engine;
    auto& w = engine.world().world();
    auto prefab = w.prefab().set<LocalTranslation>({10, 0, 0});
    auto child = w.prefab().child_of(prefab).set<LocalTranslation>({2, 0, 0});
    engine.world().evaluate_world_transforms();
    expect_near(child.get<WorldTransform>().affine.m[3], 12);
    auto instance = w.entity().is_a(prefab).set<LocalTranslation>({20, 0, 0});
    check(!instance.has<WorldTransform>(), "Prototype derived state inherited");
    unsigned children = 0;
    instance.children([&](flecs::entity e) {
        ++children;
        check(!e.has<WorldTransform>(), "Generated child copied prototype world state");
    });
    check(children == 1, "Generated prefab child fixture missing");
    engine.world().evaluate_world_transforms();
    instance.children([&](flecs::entity e) {
        check(e.owns<WorldTransform>(), "Generated child lacks owned derived state");
        expect_near(e.get<WorldTransform>().affine.m[3], 22);
    });
    Scene scene(engine.world());
    scene.reset(legacy());
    const auto id = scene.canonical_id("instance");
    authoring_command(scene, "component.revert",
                      {{"entity", id}, {"component", "forge.local_translation"}});
    auto i = scene.entity(id);
    // A world-axis gesture owns rotation only, even when all channels were inherited.
    authoring_command(scene, "transform.world_rotate",
                      {{"entity", id}, {"axis", xyz(0, 0, 1)}, {"degrees", 20}});
    check(i.owns<LocalRotation>() && !i.owns<LocalTranslation>() && !i.owns<LocalScale>(),
          "World rotation created unrelated overrides");
    authoring_command(scene, "component.revert",
                      {{"entity", id}, {"component", "forge.local_rotation"}});
    authoring_command(scene, "transform.scale", {{"entity", id}, {"value", xyz(3, 4, 5)}});
    check(i.owns<LocalScale>() && !i.owns<LocalTranslation>() && !i.owns<LocalRotation>(),
          "Scale created unrelated overrides");
    const auto base = scene.canonical_id("base");
    const auto exact = scene.entity(base).get<LocalRotation>();
    authoring_command(scene, "transform.copy_from", {{"entity", id}, {"source", base}});
    check(i.get<LocalRotation>() == exact, "Copy introduced an Euler roundtrip");
}
void generated_native_movement() {
    EngineContext engine;
    Scene scene(engine.world());
    auto source = legacy();
    source["entities"][3]["parent"] = "base";
    source["entities"][3]["prefab"] = true;
    scene.reset(source);
    flecs::entity generated;
    scene.entity("instance").children([&](flecs::entity e) { generated = e; });
    check(bool(generated), "Native generated child fixture absent");
    const auto handle = generated.id();
    scene.effective_document();
    auto before = generated.get<WorldTransform>().affine;
    scene.translate(1, 2, 3);
    scene.effective_document();
    const auto after = generated.get<WorldTransform>().affine;
    expect_near(after.m[3], before.m[3] + 1);
    expect_near(after.m[7], before.m[7] + 2);
    expect_near(after.m[11], before.m[11] + 3);
    check(generated.id() == handle && generated.owns<LocalTranslation>(),
          "Generated runtime movement rebuilt child or missed owned translation");
    // Following the moved instance must apply its world displacement only once.
    generated.set<SpatialBinding>({SpatialMode::FollowStructure, {}});
    scene.effective_document();
    before = generated.get<WorldTransform>().affine;
    scene.translate(1, 2, 3);
    scene.effective_document();
    expect_near(generated.get<WorldTransform>().affine.m[3], before.m[3] + 1);
    expect_near(generated.get<WorldTransform>().affine.m[7], before.m[7] + 2);
    expect_near(generated.get<WorldTransform>().affine.m[11], before.m[11] + 3);
}
void affine_operations() {
    EngineContext engine;
    Scene s(engine.world());
    auto p = create(s, 2), c = create(s, 1), external = create(s, 9);
    s.entity(p).set<LocalRotation>(rotation_from_euler({0, 25, 30}));
    s.entity(p).set<LocalScale>({2, 3, 4});
    s.entity(c).set<LocalRotation>(rotation_from_euler({15, 0, 35}));
    s.reparent_entity(c, p, ReparentMode::KeepLocal);
    const auto rotation = s.entity(c).get<LocalRotation>();
    const auto scale = s.entity(c).get<LocalScale>();
    authoring_command(s, "transform.world_translation", {{"entity", c}, {"value", xyz(7, 8, 9)}});
    expect_near(world(s, c).m[3], 7);
    expect_near(world(s, c).m[7], 8);
    expect_near(world(s, c).m[11], 9);
    check(s.entity(c).get<LocalRotation>() == rotation && s.entity(c).get<LocalScale>() == scale,
          "World move changed other local channels");
    const auto view = row(s.effective_document(), c);
    ObjectTransform geometry(view);
    const auto point = geometry.point({.2f, .3f, .4f});
    const auto expected = world(s, c).point({.2, .3, .4});
    for (unsigned a = 0; a < 3; ++a)
        expect_near(point[a], expected[a], 2e-6);
    rejects([&] { ObjectTransform invalid(row(s.document(), c)); });
    auto doc = s.document();
    for (auto& e : doc["entities"])
        if (e.at("id") == c) {
            e["spatial"] = {{"mode", "explicit"}, {"target", s.reference(p)}};
            e.erase("parent");
        }
    s.edit(doc);
    const auto before = s.document();
    const auto revision = s.revision();
    rejects([&] { s.delete_subtree(p); });
    check(s.document() == before && s.revision() == revision,
          "Sheared dependent deletion partially committed");
    // Subtree duplication remaps known internal bindings only.
    s.reparent_entity(c, p, ReparentMode::KeepLocal);
    doc = s.document();
    for (auto& e : doc["entities"])
        if (e.at("id") == c) {
            e["spatial"] = {{"mode", "explicit"}, {"target", s.reference(p)}};
            e["plugin"] = {{"ref", s.reference(p)}};
        }
    s.edit(doc);
    auto duplicated = s.duplicate_subtree(p);
    doc = s.document();
    for (const auto& e : doc.at("entities"))
        if (e.value("parent", std::string{}) == duplicated) {
            check(e.at("spatial").at("target").get<EntityRef>() == s.reference(duplicated),
                  "Internal attachment not remapped");
            check(e.at("plugin").at("ref").get<EntityRef>() == s.reference(p),
                  "Opaque payload was rewritten");
        }
    authoring_command(s, "transform.binding",
                      {{"entity", c},
                       {"mode", "keep_local"},
                       {"spatial", {{"mode", "explicit"}, {"target", s.reference(external)}}}});
    const auto copied = s.duplicate_subtree(p);
    doc = s.document();
    for (const auto& e : doc.at("entities"))
        if (e.value("parent", std::string{}) == copied)
            check(e.at("spatial").at("target").get<EntityRef>() == s.reference(external),
                  "External attachment was remapped");
}
void files_and_scope() {
    const auto folder =
        std::filesystem::current_path() / ("transforms-" + AssetId::generate().str());
    std::filesystem::create_directory(folder);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{folder};
    auto v2 = legacy();
    v2["version"] = 2;
    v2["asset_id"] = AssetId::generate();
    std::map<std::string, std::string> ids;
    for (auto& e : v2["entities"]) {
        auto old = e.at("id").get<std::string>();
        ids[old] = EntityId::generate().str();
        e["id"] = ids.at(old);
    }
    for (auto& e : v2["entities"])
        for (auto relation : {"parent", "base"})
            if (e.contains(relation))
                e[relation] = ids.at(e.at(relation));
    v2["legacy_ids"] = ids;
    const auto path = folder / "old.scene.json";
    const auto bytes = v2.dump(4);
    atomic_write(path, bytes);
    const auto migrated = read_scene_file(path);
    check(read_scene_file(path) == migrated, "V2 migration is not repeatable");
    auto read = [](const auto& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    check(read(path) == bytes && migrated.at("asset_id") == v2.at("asset_id"),
          "Open changed source or AssetId");
    for (unsigned i = 0; i < 4; ++i)
        check(migrated["entities"][i]["id"] == v2["entities"][i]["id"], "V3 changed EntityId");
    std::filesystem::create_directory(path.string() + ".v2.backup.pending");
    rejects([&] { write_scene_file(path, migrated); });
    check(read(path) == bytes, "Failed backup lost original");
    std::filesystem::remove(path.string() + ".v2.backup.pending");
#ifdef _WIN32
    auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "Cannot create blocked Windows migration fixture");
    rejects([&] { write_scene_file(path, migrated); });
    CloseHandle(lock);
#else
    std::filesystem::create_directory(path.string() + ".pending");
    rejects([&] { write_scene_file(path, migrated); });
    std::filesystem::remove(path.string() + ".pending");
#endif
    check(read(path) == bytes && read(path.string() + ".v2.backup") == bytes,
          "Failed replacement lost original bytes");
    write_scene_file(path, migrated);
    check(read_scene_file(path) == migrated, "V3 save/reopen drift");
    EngineContext engine;
    Scene scene(engine.world());
    scene.load(path);
    const auto expected = world(scene, "instance");
    check(!scene.entity("instance").owns<LocalRotation>() &&
              !scene.entity("instance").owns<LocalScale>(),
          "Disk migration materialized inherited channels");
    scene.reset(empty_scene());
    scene.load(path);
    expect_near(world(scene, "instance"), expected);
    const auto p = create(scene, 7), c = create(scene, 9);
    authoring_command(
        scene, "transform.binding",
        {{"entity", c}, {"spatial", {{"mode", "explicit"}, {"target", scene.reference(p)}}}});
    auto saved = scene.document();
    auto copy = duplicate_scene_asset(saved);
    const auto original_ref = row(saved, c).at("spatial").at("target").get<EntityRef>();
    const auto copied_ref = copy["entities"].back().at("spatial").at("target").get<EntityRef>();
    check(copied_ref.scene == copy.at("asset_id").get<AssetId>() &&
              copied_ref.entity != original_ref.entity,
          "Scene duplication did not remap spatial reference");
    Scene loaded(engine.world());
    loaded.reset(saved);
    scene.entity(p).set<LocalTranslation>({17, 0, 0});
    expect_near(world(scene, c).m[3], 19);
    expect_near(world(loaded, c).m[3], 9);
    auto before = loaded.document();
    rejects([&] { loaded.reparent_entity(p, c); });
    check(loaded.document() == before, "Mixed FollowStructure/Explicit cycle accepted");
    scene.reset(empty_scene());
    expect_near(world(loaded, c).m[3], 9);
    check(!engine.world().world().lookup("forge.local_transform"),
          "Combined LocalTransform registered as authority");
}

} // namespace
int main() {
    try {
        math();
        migration_and_inheritance();
        hierarchy();
        compensation();
        files_and_scope();
        generated_and_channels();
        affine_operations();
        generated_native_movement();
        std::cout << "Transform math, inheritance, migration, hierarchy, channels, cycles, history "
                     "and native movement passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
