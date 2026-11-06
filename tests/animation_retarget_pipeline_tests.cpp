#include "animation_blend_space_authoring.hpp"
#include "animation_clip_selection.hpp"
#include "animation_resource.hpp"
#include "animation_retarget_authoring.hpp"
#include "animation_states_authoring.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "model_authoring.hpp"
#include "model_importer.hpp"
#include "model_placement.hpp"
#include "model_skeleton_binding.hpp"
#include "runtime_package.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
using namespace forge::animation_detail;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run, const char* message) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, const Json& doc) {
    const auto text = doc.dump();
    std::ofstream output(path, std::ios::binary);
    require(bool(output.write(text.data(), std::streamsize(text.size()))), "Fixture write failed");
}
AssetId publish_source(AssetImportService& service, const std::filesystem::path& source,
                       const AssetImportService::PreparePublication& prepare,
                       AssetId identity = {}) {
    auto draft = service.prepare(source, {}, identity);
    const auto id = draft.request.asset;
    service.submit(std::move(draft), prepare, [](const auto&, const auto&) {});
    require(service.wait_idle(60s), "Retarget fixture publication timed out");
    const auto rows = service.poll();
    require(rows.size() == 1 && rows.front().published,
            rows.empty() ? "Missing fixture import receipt" : rows.front().diagnostic);
    return id;
}
void near(double a, double b) {
    require(std::isfinite(a) && std::abs(a - b) < .004, "Retarget target pose differs");
}
void ready(const std::shared_ptr<AnimationRuntime>& animation) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!animation->prepare_initial_pose()) {
        require(std::chrono::steady_clock::now() < deadline, "Baked runtime pose timed out");
        std::this_thread::sleep_for(1ms);
    }
}
Json playback(const std::filesystem::path& project, const Json& source, EntityId actor,
              float seconds, const Json* checkpoint = nullptr) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(project)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto animation = animation_runtime(engine.world());
    ready(animation);
    if (checkpoint)
        animation->restore(*checkpoint);
    else
        animation->tick(seconds, 1);
    return animation->presentation(scene.entity(actor.str()).id(), 1);
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool prepare_editor = argc == 6 && std::string_view(argv[5]) == "--prepare";
        require(argc == 5 || prepare_editor,
                "Need scratch, original source, model worker and converter; optional --prepare");
        const auto project = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(project / "Assets");
        const auto original = read_bytes(argv[2], 1024 * 1024);
        auto source = parse_bounded_json(original, 1024 * 1024);
        auto destination = source;
        auto base = source.at("animations")[0];
        Json channels = Json::array();
        for (const auto& channel : base.at("channels"))
            if (channel.at("target").at("path") != "weights")
                channels.push_back(channel);
        base["channels"] = std::move(channels);
        source["animations"] = Json::array({base});
        destination["animations"] = Json::array({destination.at("animations")[2]});
        const float h = float(std::sqrt(.5));
        destination["nodes"][0]["rotation"] = {0, h, 0, h};
        destination["nodes"][1]["rotation"] = {0, 0, h, h};
        destination["nodes"][1]["translation"] = {0, 2, 0};
        destination["nodes"][4]["translation"] = {0, 1.5, 0};
        write(project / "Assets/Source.gltf", source);
        write(project / "Assets/Target.gltf", destination);
        auto lease = std::make_shared<ProjectLease>(project);
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(
            model_importer(std::filesystem::absolute(argv[3]), std::filesystem::absolute(argv[4])));
        registry->seal();
        AssetImportService models(lease, registry, {"linux", "none", "cpu"}, 1);
        const auto publish_model = [](auto& c, const auto& p, const auto& cat) {
            prepare_model_publication(c, p, cat);
        };
        const auto source_id = publish_source(models, "Assets/Source.gltf", publish_model);
        const auto target_id = publish_source(models, "Assets/Target.gltf", publish_model);
        auto catalog = AssetCatalog::open_project(project);
        const auto from = load_model_selection(project, catalog, source_id);
        const auto to = load_model_selection(project, catalog, target_id);
        const auto source_skeleton = from.bindings.at("/rig/skeleton");
        const auto source_clip = from.bindings.at("/animations/0");
        const auto target_skeleton = to.bindings.at("/rig/skeleton");
        rejects([&] { select_animation_clip(catalog, {target_skeleton}, {source_clip}); },
                "Foreign imported clip was accepted for the target skeleton");
        auto document = AnimationRetargetDocument::create(
            lease, "Assets/Motion.retarget.json", [&](AssetId id) {
                auto value = AnimationRetargetSource::create(id);
                value.document["source_skeleton"] = source_skeleton;
                value.document["source_clip"] = source_clip;
                value.document["target_skeleton"] = target_skeleton;
                value.document["root_translation"] = true;
                value.document["translation_scale"] = 2.5;
                for (const auto joint : model_skeleton_joints(from, source_skeleton).assets)
                    value.document["entries"].push_back(
                        {{"source", joint},
                         {"target", to.bindings.at(from.member(joint).identity.address)}});
                return value;
            });
        const auto baked = document->source().asset();
        const auto initial_recipe = document->source().document;
        const auto initial_revision = document->revision();
        Json deep = 1;
        for (unsigned i = 0; i < 14; ++i)
            deep = Json::array({deep});
        for (const auto& extension : {deep, Json(std::string(animation_retarget_byte_limit, 'x')),
                                      Json(std::vector<unsigned>(8000, 0))}) {
            rejects(
                [&] {
                    document->edit(initial_revision, "Reject unreopenable recipe",
                                   [&](auto& value) { value["extension"] = extension; });
                },
                "Retarget edit accepted a recipe beyond its reopen limits");
            require(document->source().document == initial_recipe &&
                        document->revision() == initial_revision && !document->can_undo(),
                    "Rejected retarget structural edit changed source/history");
        }
        document->edit(document->revision(), "Change retarget sampling rate",
                       [](auto& value) { value["sampling_rate"] = 60; });
        document->undo();
        require(document->source().document.at("sampling_rate") == 30,
                "Retarget source Undo failed");
        document->redo();
        require(document->source().document.at("sampling_rate") == 60,
                "Retarget source Redo failed");
        document->save();
        AssetImportService retargets(lease, animation_retarget_import_registry(),
                                     {"linux", "none", "cpu"}, 1);
        publish_source(
            retargets, document->locator(),
            [](auto& c, const auto& p, const auto&) {
                prepare_animation_retarget_publication(c, p);
            },
            baked);
        if (prepare_editor) {
            std::cout << project.string() << '\n';
            std::cout << "Retarget source/target publication prepared for native editor fixture\n";
            return 0;
        }
        catalog = AssetCatalog::open_project(project);
        const auto binding = select_animation_clip(catalog, {target_skeleton}, {baked});
        require(binding.baked && binding.model == target_id &&
                    binding.skeleton_revision == to.revision &&
                    binding.clip_revision != to.revision,
                "Baked target binding lost independent immutable revisions");
        const auto cooked = load_baked_animation_clip(project, catalog.records().at(baked), to);
        auto target_native = std::make_shared<Skeleton>(to.bytes(to.member(target_skeleton)));
        Sampler sampler(target_native, cooked.native);
        auto source_native = std::make_shared<Skeleton>(from.bytes(from.member(source_skeleton)));
        auto source_motion = std::make_shared<Clip>(from.bytes(from.member(source_clip)));
        Sampler original_sampler(source_native, source_motion);
        const auto joints = model_skeleton_joints(to, target_skeleton);
        double motion = 0;
        for (const auto ratio : {0.f, .25f, .5f, 1.f}) {
            (void)sampler.sample(ratio);
            (void)original_sampler.sample(ratio);
            const auto local = sampler.local_pose(), original_local = original_sampler.local_pose();
            for (std::size_t joint = 0; joint < local.size(); ++joint) {
                const auto rest = target_native->joint_rest_pose(joint);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const auto expected =
                        joints.parents[joint] < 0
                            ? rest.translation[axis] +
                                  (original_local[joint].translation[axis] -
                                   source_native->joint_rest_pose(joint).translation[axis]) *
                                      2.5
                            : rest.translation[axis];
                    near(local[joint].translation[axis], expected);
                    near(local[joint].scale[axis], 1);
                }
                if (joint == 1)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        motion = std::max(motion, std::abs(double(local[joint].rotation[lane]) -
                                                           rest.rotation[lane]));
            }
        }
        require(motion > .01, "Retargeted target joint did not animate");
        ModelAnimationResources resources(project);
        const auto request = resources.request(std::make_shared<const AssetCatalog>(catalog),
                                               {target_skeleton}, {baked});
        require(resources.prepare_recovery(request, 10s), "Baked target resources timed out");
        auto held = resources.acquire(request);
        require(held && held.clip->has_transform_channels &&
                    held.clip->transform_channels.size() == joints.assets.size() * 3 &&
                    held.clip.identity().revision == binding.clip_revision &&
                    held.skeleton.identity().revision == binding.skeleton_revision,
                "Baked target resources lost target channel intent or independent revisions");
        rejects([&] { select_animation_clip(catalog, {source_skeleton}, {baked}); },
                "Baked clip was accepted with a different skeleton");
        auto altered = catalog;
        auto invalid = catalog.records().at(baked);
        invalid.metadata["forge.animation_clip"]["skeleton_revision"] = from.revision;
        altered.replace(invalid);
        rejects([&] { select_animation_clip(altered, {target_skeleton}, {baked}); },
                "Baked clip accepted a stale skeleton revision");
        auto too_small = [&] { load_model_selection(project, catalog, source_id, {}, 64); };
        rejects(too_small, "Retarget selected model byte bound was ignored");
        // Actual AnimationRuntime realizes the ordinary Animator after its clip
        // is assigned to the new target-bound asset. Imported instance IDs remain.
        EngineContext engine(WorldRole::Runtime, false, {animation_module(project)});
        Scene scene(engine.world());
        ModelPlacementOptions options;
        options.clip = {to.bindings.at("/animations/0")};
        const auto placement =
            prepare_model_placement(to, scene.asset_id(), scene.revision(), options);
        const auto root = instantiate_model(scene, catalog, placement);
        const auto actor = scene.entity(root.str());
        auto animator = actor.get<Animator>();
        animator.clip = {baked};
        animator.loop = false;
        actor.set<Animator>(animator);
        const auto authored = scene.document();
        const auto animation = animation_runtime(engine.world());
        ready(animation);
        animation->tick(.5f, 1);
        const auto pose = animation->presentation(actor.id(), 1);
        require(pose.at("clip") == Json(baked), "Runtime did not select retargeted clip");
        require(animation->checkpoint_ready(), "Retargeted runtime is not checkpoint ready");
        const auto checkpoint = animation->checkpoint();
        require(!checkpoint.is_null(), "Retargeted checkpoint is absent");
        require(playback(project, authored, root, 0, &checkpoint).at("local") == pose.at("local"),
                "Retargeted cold recovery changed the target pose");
        auto states =
            AnimationStatesDocument::create(lease, "Assets/Target.states.json", [&](AssetId id) {
                auto value = AnimationStatesSource::create(id);
                value.document["skeleton"] = target_skeleton;
                value.document["entry"] = 1;
                value.document["next_id"] = 3;
                value.document["states"] = {{{"id", 1},
                                             {"name", "Rest"},
                                             {"clip", to.bindings.at("/animations/0")},
                                             {"loop", false},
                                             {"speed", 1},
                                             {"transition_seconds", 0}},
                                            {{"id", 2},
                                             {"name", "Retargeted"},
                                             {"clip", baked},
                                             {"loop", false},
                                             {"speed", 1},
                                             {"transition_seconds", .2}}};
                return value;
            });
        AssetImportService state_imports(lease, animation_states_import_registry(),
                                         {"linux", "none", "cpu"}, 1);
        const auto states_id = publish_source(
            state_imports, states->locator(),
            [](auto& c, const auto& p, const auto&) { prepare_animation_states_publication(c, p); },
            states->source().asset());
        auto state_scene = authored;
        for (auto& row : state_scene["entities"])
            if (row.at("id") == Json(root)) {
                row["components"]["forge.animator"]["states"] = states_id;
                row["components"]["forge.animator"]["state"] = 2;
            }
        const auto state_pose = playback(project, state_scene, root, .5f);
        require(state_pose.at("states") == Json(states_id) && state_pose.at("clip") == Json(baked),
                "Animation States did not play the target-bound clip");
        auto blend = AnimationBlendSpaceDocument::create(
            lease, "Assets/Target.blendspace.json", [&](AssetId id) {
                auto value = AnimationBlendSpaceSource::create(id);
                value.document["skeleton"] = target_skeleton;
                value.document["samples"] = {
                    {{"id", AssetId::generate()},
                     {"position", 0},
                     {"clip", to.bindings.at("/animations/0")}},
                    {{"id", AssetId::generate()}, {"position", 1}, {"clip", baked}}};
                return value;
            });
        AssetImportService blend_imports(lease, animation_blend_space_import_registry(),
                                         {"linux", "none", "cpu"}, 1);
        const auto blend_id = publish_source(
            blend_imports, blend->locator(),
            [](auto& c, const auto& p, const auto&) {
                prepare_animation_blend_space_publication(c, p);
            },
            blend->source().asset());
        auto blend_scene = authored;
        for (auto& row : blend_scene["entities"])
            if (row.at("id") == Json(root)) {
                row["components"]["forge.animator"]["blend_space"] = blend_id;
                row["components"]["forge.animator"]["blend_position"] = .5;
            }
        const auto blended = playback(project, blend_scene, root, .5f);
        require(blended.at("blend_space") == Json(blend_id),
                "Blend Space did not realize target-bound samples");
        (void)sampler.sample(.5f);
        const auto halfway = sampler.local_pose();
        for (unsigned axis = 0; axis < 3; ++axis)
            near(blended.at("local")[0].at("translation")[axis],
                 (halfway[0].translation[axis] +
                  target_native->joint_rest_pose(0).translation[axis]) *
                     .5);
        // Discovery and cooking share exact source/dependency snapshots. Reject
        // a stale source without advancing the selected catalog or losing history.
        const auto before = AssetCatalog::open_project(project).document();
        const auto draft = retargets.prepare(document->locator());
        const auto plan = draft.importer->discover(draft.request, {});
        document->edit(document->revision(), "Change bake rate",
                       [](auto& value) { value["sampling_rate"] = 120; });
        document->save();
        rejects([&] { draft.importer->import_and_cook(draft.request, plan, {}, {}); },
                "Stale retarget source was cooked");
        require(AssetCatalog::open_project(project).document() == before,
                "Rejected stale retarget changed publication");
        document->undo();
        document->save();
        const auto exported = project.parent_path() / (project.filename().string() + "-export");
        const std::array roots{baked, target_id, states_id, blend_id};
        package_runtime_content(project, exported, roots, {"linux", "none"});
        const auto moved = project.parent_path() / (project.filename().string() + "-relocated");
        std::filesystem::rename(exported, moved);
        std::filesystem::rename(project / "Assets/Source.gltf", project / "Assets/Source.hidden");
        std::filesystem::rename(project / "Assets/Target.gltf", project / "Assets/Target.hidden");
        std::filesystem::rename(project / "Assets/Motion.retarget.json",
                                project / "Assets/Motion.hidden");
        const auto shipped = open_runtime_content(moved, {"linux", "none"});
        require(!shipped.records().contains(source_id) &&
                    !shipped.records().contains(source_skeleton) &&
                    !shipped.records().contains(source_clip) &&
                    !std::filesystem::exists(moved / "Assets/Motion.retarget.json") &&
                    !std::filesystem::exists(moved / "Assets/Target.gltf"),
                "Retarget source/build-only model data entered runtime content");
        require(playback(moved, authored, root, .5f).at("local") == pose.at("local"),
                "Relocated source-hidden retarget playback differs");
        require(playback(moved, state_scene, root, .5f).at("local") == state_pose.at("local") &&
                    playback(moved, blend_scene, root, .5f).at("local") == blended.at("local"),
                "Relocated source-hidden States/Blend Space target playback differs");
        std::cout << project.string() << '\n';
        std::cout << "Retarget immutable publication/source history/exact target resources/"
                     "numeric motion/bounds/actual runtime/recovery/States/Blend Space/"
                     "relocated source-hidden target-only export passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
