#include <bit>
#include <forge/animation.hpp>
#include <forge/animation_conversion.hpp>
#include <forge/assets.hpp>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
struct Fixture {
    Module module;
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    explicit Fixture(const std::filesystem::path& root)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene, module), animation(animation_runtime(engine.world())) {}
    void load(const Json& doc) {
        scene.restore_snapshot(doc);
        simulation.reset_presentation();
    }
    Json pose(double alpha = 1) {
        return animation->presentation(scene.entity("actor").id(), alpha);
    }
};
#include "animation_signed_scale.hpp"
int main(int argc, char** argv) {
    try {
        check((argc == 4 || argc == 5), "Need project, converter and source paths");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        std::filesystem::copy_file(argv[3], root / "Assets/source.gltf");
        auto convert = [&](const char* source = "Assets/source.gltf") {
            return prepare_animation_conversion(root, source, std::filesystem::absolute(argv[2]));
        };
        auto records = convert().publish();
        for (const auto& record : records) {
            check(!record.source_dependencies.empty(), "Legacy animation lacks source graph edges");
            if (record.type != "animation_source")
                check(record.dependency_edges.size() == record.dependencies.size(),
                      "Legacy animation dependency types missing");
        }
        AssetRecord skeleton, clip, source;
        for (const auto& record : records) {
            if (record.type == SkeletonAsset::type)
                skeleton = record;
            else if (record.type == AnimationClipAsset::type)
                clip = record;
            else
                source = record;
        }
        check(bool(skeleton.id) && bool(clip.id) && bool(source.id),
              "Conversion missed generated identity");
        if (argc == 5) {
            check(std::string(argv[4]) == "--prepare", "Unknown fixture mode");
            Json config = {{"skeleton", skeleton.id}, {"clip", clip.id}, {"enabled", true},
                           {"play_on_start", true},   {"loop", true},    {"playback_speed", 1}};
            Json doc = {
                {"version", 1},
                {"entities", Json::array({{{"id", "actor"},
                                           {"name", "Actor"},
                                           {"components",
                                            {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                             {"forge.animator", config}}}}})}};
            std::ofstream(root / "scene.json") << doc.dump();
            std::cout << root.string() << "\n";
            return 0;
        }
        signed_scale_animation(root / "signed", std::filesystem::absolute(argv[2]),
                               Json::parse(read(root / "Assets/source.gltf")));
        // Pinned converter fallback channels must retain matrix-authored parent rest.
        const auto matrix_root = root / "matrix-rest";
        std::filesystem::create_directories(matrix_root / "Assets");
        auto matrix_source = Json::parse(read(root / "Assets/source.gltf"));
        matrix_source["nodes"][0]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1};
        const auto original_matrix_text = matrix_source.dump();
        {
            std::ofstream out(matrix_root / "Assets/source.gltf");
            out << original_matrix_text;
        }
        const auto matrix_records = prepare_animation_conversion(matrix_root, "Assets/source.gltf",
                                                                 std::filesystem::absolute(argv[2]))
                                        .publish();
        AssetId matrix_skeleton, matrix_clip;
        for (const auto& record : matrix_records) {
            if (record.type == SkeletonAsset::type)
                matrix_skeleton = record.id;
            if (record.type == AnimationClipAsset::type)
                matrix_clip = record.id;
        }
        Json matrix_config = {
            {"skeleton", matrix_skeleton}, {"clip", matrix_clip}, {"enabled", true},
            {"play_on_start", true},       {"loop", false},       {"playback_speed", 1}};
        Json matrix_scene = {
            {"version", 1},
            {"entities", Json::array({{{"id", "actor"},
                                       {"name", "Actor"},
                                       {"components",
                                        {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                         {"forge.animator", matrix_config}}}}})}};
        Fixture matrix_runtime(matrix_root);
        matrix_runtime.load(matrix_scene);
        matrix_runtime.simulation.tick(.5f);
        const auto matrix_pose = matrix_runtime.pose();
        check(!matrix_pose.is_null() &&
                  std::abs(matrix_pose["model"][0][12].get<double>() - 5) < .002 &&
                  std::abs(matrix_pose["model"][1][12].get<double>() - 5) < .002 &&
                  std::abs(matrix_pose["model"][1][13].get<double>() - 1.5) < .002,
              "Matrix parent rest lost in legacy animation fallback");
        check(read(matrix_root / "Assets/source.gltf") == original_matrix_text,
              "Animation conversion rewrote matrix source");
        const auto matrix_catalog = read(AssetCatalog::project_index(matrix_root));
        for (int bad_case = 0; bad_case < 4; ++bad_case) {
            auto bad = matrix_source;
            if (bad_case == 0)
                bad["nodes"][0]["matrix"][3] = 1;
            if (bad_case == 1)
                bad["nodes"][0]["matrix"][4] = .5;
            if (bad_case == 2)
                bad["nodes"][0]["matrix"][0] = 0;
            if (bad_case == 3)
                bad["nodes"][0]["translation"] = {1, 2, 3};
            {
                std::ofstream out(matrix_root / "Assets/source.gltf");
                out << bad.dump();
            }
            reject([&] {
                prepare_animation_conversion(matrix_root, "Assets/source.gltf",
                                             std::filesystem::absolute(argv[2]));
            });
            check(read(AssetCatalog::project_index(matrix_root)) == matrix_catalog,
                  "Invalid matrix rest changed selected animation");
        }
        auto again = convert().publish();
        for (const auto& record : again)
            check(record.id == skeleton.id || record.id == clip.id || record.id == source.id,
                  "Reconversion changed AssetId");
        auto catalog = AssetCatalog::open_project(root);
        check(catalog.records().at(clip.id).metadata == clip.metadata,
              "Deterministic provenance changed on same source");
        const auto baseline = read(AssetCatalog::project_index(root));
        auto stale = convert();
        std::ofstream(root / "Assets/source.gltf") << "invalid";
        reject([&] { stale.publish(); });
        reject([&] { convert(); });
        check(read(AssetCatalog::project_index(root)) == baseline,
              "Failed conversion changed known good catalog");
        std::filesystem::copy_file(argv[3], root / "Assets/source.gltf",
                                   std::filesystem::copy_options::overwrite_existing);
        std::stop_source stop;
        stop.request_stop();
        reject([&] {
            prepare_animation_conversion(root, "Assets/source.gltf",
                                         std::filesystem::absolute(argv[2]), stop.get_token());
        });
        reject([&] { convert("../escape.gltf"); });
        auto bad = Json::parse(read(root / "Assets/source.gltf"));
        bad["buffers"][0]["uri"] = "../../escape.bin";
        std::ofstream(root / "Assets/bad.gltf") << bad.dump();
        reject([&] { convert("Assets/bad.gltf"); });
        bad["buffers"][0]["uri"] = "https://example.invalid/data.bin";
        std::ofstream(root / "Assets/bad.gltf") << bad.dump();
        reject([&] { convert("Assets/bad.gltf"); });
        check(read(AssetCatalog::project_index(root)) == baseline,
              "Cancellation/path failure published assets");
        // ID-aware source move preserves generated IDs and exact clip binding.
        std::filesystem::rename(root / "Assets/source.gltf", root / "Assets/moved.gltf");
        catalog.relocate(source.id, "Assets/moved.gltf");
        catalog.save(AssetCatalog::project_index(root));
        auto moved = convert("Assets/moved.gltf").publish();
        for (const auto& record : moved)
            check(record.id == skeleton.id || record.id == clip.id || record.id == source.id,
                  "Source move broke identity");
        catalog = AssetCatalog::open_project(root);
        skeleton = catalog.records().at(skeleton.id);
        clip = catalog.records().at(clip.id);
        Json config = {{"skeleton", skeleton.id}, {"clip", clip.id}, {"enabled", true},
                       {"play_on_start", true},   {"loop", false},   {"playback_speed", 1}};
        Json doc = {{"version", 1},
                    {"entities", Json::array({{{"id", "actor"},
                                               {"name", "Actor"},
                                               {"components",
                                                {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                                 {"forge.animator", config}}}}})}};
        Fixture f(root);
        f.load(doc);
        auto authored = f.scene.snapshot();
        check(!f.pose().is_null(), "Animator did not realize");
        check(std::abs(f.pose()["model"][1][13].get<double>() - 1) < .002, "Start pose mismatch");
        // Presentation reads cannot advance authoritative state.
        const auto initial = f.animation->checkpoint();
        for (int i = 0; i < 10; ++i)
            f.pose(.8);
        check(f.animation->checkpoint() == initial, "Presentation advanced playback");
        f.simulation.tick(.5f);
        check(std::abs(f.pose()["model"][1][13].get<double>() - 1.5) < .002,
              "Fixed-tick midpoint mismatch");
        check(std::abs(f.pose(.5)["model"][1][13].get<double>() - 1.25) < .002,
              "Presentation did not sample interpolated time");
        auto recovery = f.animation->checkpoint();
        Fixture recovered(root);
        recovered.load(authored);
        recovered.animation->restore(recovery);
        recovered.simulation.reset_presentation();
        check(recovered.pose() == f.pose(), "Recovery did not reconstruct same pose");
        auto invalid = recovery;
        invalid["entries"][0]["clip_revision"] = "invalid";
        reject([&] { recovered.animation->restore(invalid); });
        f.simulation.tick(.5f);
        check(std::abs(f.pose()["time"].get<double>() - 1) < 1e-6 &&
                  !f.pose()["playing"].get<bool>(),
              "Nonloop did not finish");
        f.simulation.tick(.5f);
        check(std::abs(f.pose()["time"].get<double>() - 1) < 1e-6, "Finished clip advanced");
        check(f.scene.snapshot() == authored, "Animation changed authored scene");
        Fixture loop(root);
        auto loopdoc = doc;
        loopdoc["entities"][0]["components"]["forge.animator"]["loop"] = true;
        loop.load(loopdoc);
        loop.simulation.tick(.75f);
        loop.simulation.tick(.5f);
        check(std::abs(loop.pose()["time"].get<double>() - .25) < 1e-6, "Loop wrap failed");
        check(std::abs(loop.pose(.25)["time"].get<double>() - .875) < 1e-6,
              "Loop presentation interpolated matrices/wrong direction");
        auto actor = loop.scene.entity("actor");
        auto a = actor.get<Animator>();
        a.playback_speed = 2;
        actor.set(a);
        loop.simulation.tick(.1f);
        check(std::abs(loop.pose()["time"].get<double>() - .45) < 1e-5,
              "Native Animator update missed");
        a.enabled = false;
        actor.set(a);
        loop.simulation.tick(.5f);
        check(loop.pose().is_null(), "Disabled Animator rendered");
        a.enabled = true;
        actor.set(a);
        loop.simulation.tick(.1f);
        check(!loop.pose().is_null(), "Reenabled Animator missing");
        // Generic authoring and structured prefabs preserve independent/equal overrides.
        EngineContext edit;
        Scene scene(edit.world());
        scene.restore_snapshot(doc);
        auto prefab = create_prefab_source(scene, "actor");
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        auto instance = instantiate_prefab(scene, prefab.asset());
        authoring_command(scene, "property.set",
                          {{"entity", instance},
                           {"component", "forge.animator"},
                           {"field", "playback_speed"},
                           {"value", 1}});
        auto saved = scene.snapshot();
        check(scene.undo() && scene.redo(), "Animator undo/redo failed");
        check(scene.snapshot() == saved, "Animator undo/redo differs");
        auto duplicate = authoring_command(scene, "entity.duplicate", {{"entity", instance}})
                             .at("selected")
                             .get<std::string>();
        check(scene.entity(duplicate).get<Animator>().clip.id == clip.id,
              "Prefab duplication lost clip reference");
        auto changed = prefab.source;
        changed["revision"] = 2;
        for (auto& member : changed["members"])
            if (member["id"] == changed["root"])
                member["components"]["forge.animator"]["playback_speed"] = 2;
        scene.set_prefab_sources({{prefab.asset(), changed}});
        check(scene.entity(instance).get<Animator>().playback_speed == 1,
              "Equal-value override was lost");
        authoring_command(
            scene, "property.revert",
            {{"entity", instance}, {"component", "forge.animator"}, {"field", "playback_speed"}});
        check(scene.entity(instance).get<Animator>().playback_speed == 2,
              "Animator property Revert failed");
        Scene reopened(edit.world());
        reopened.restore_snapshot(scene.snapshot());
        check(reopened.entity(instance).get<Animator>() == scene.entity(instance).get<Animator>(),
              "Animator scene roundtrip differs");
        // Wrong binding and changed bytes fail before sampling, preserving catalog and authored
        // data.
        auto corrupted = clip;
        corrupted.metadata["skeleton_sha256"] = std::string(64, '0');
        catalog.replace(corrupted);
        catalog.save(AssetCatalog::project_index(root));
        Fixture incompatible(root);
        incompatible.load(doc);
        check(incompatible.pose().is_null(), "Wrong skeleton binding accepted");
        check(!incompatible.engine.services().diagnostics().empty(),
              "Missing structured animation diagnostic");
        reject([&] { incompatible.animation->restore(recovery); });
        catalog.replace(clip);
        catalog.save(AssetCatalog::project_index(root));
        auto artifact = root / clip.source;
        auto good = read(artifact);
        std::ofstream(artifact, std::ios::binary) << "bad";
        Fixture corrupt(root);
        corrupt.load(doc);
        check(corrupt.pose().is_null(), "Digest mismatch reached playback");
        std::ofstream(artifact, std::ios::binary) << good;
        check(read(AssetCatalog::project_index(root)) != baseline,
              "Source relocation did not persist");
        // A contained relative buffer dependency is copied, hashed and converted.
        auto external = Json::parse(read(root / "Assets/moved.gltf"));
        external["buffers"][0]["uri"] = "animation.bin";
        {
            std::ofstream binary(root / "Assets/animation.bin", std::ios::binary);
            for (float value : {0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 2.f, 0.f}) {
                auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned i = 0; i < 4; ++i)
                    binary.put(char((bits >> (8 * i)) & 255));
            }
        }
        std::ofstream(root / "Assets/external.gltf") << external.dump();
        auto dependencies = convert("Assets/external.gltf").publish();
        for (const auto& record : dependencies)
            if (record.type == AnimationClipAsset::type)
                check(record.metadata.at("source_dependencies").size() == 1 &&
                          record.source_dependencies.size() == 2,
                      "Relative buffer provenance missing");
        const auto before_rename = read(AssetCatalog::project_index(root));
        auto weights = external;
        weights["animations"][0]["channels"][0]["target"]["path"] = "weights";
        std::ofstream(root / "Assets/weights.gltf") << weights.dump();
        reject([&] { convert("Assets/weights.gltf"); });
        external["animations"][0]["name"] = "Renamed";
        std::ofstream(root / "Assets/external.gltf") << external.dump();
        reject([&] { convert("Assets/external.gltf"); });
        check(read(AssetCatalog::project_index(root)) == before_rename,
              "Clip rename silently retargeted references");
        std::filesystem::remove_all(root);
        std::cout << "Animation conversion, identity, fixed playback, recovery, prefab and "
                     "rejection tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
