#include "animation_playback_access.hpp"
#include "fixtures/skeletal_sequence_gallery.hpp"
#include <algorithm>
#include <cmath>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > .003)
        throw std::runtime_error("Skeletal Gallery sample differs: " + std::to_string(actual) +
                                 " vs " + std::to_string(expected));
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false,
                 {runtime_resources_module(root), animation_module(root)}),
          scene(engine.world()), simulation(engine.world(), scene),
          animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        for (;;) {
            engine.services().resources()->synchronize();
            if (animation->prepare_initial_pose())
                break;
            check(std::chrono::steady_clock::now() < deadline,
                  "Skeletal sequence Gallery assets stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
    }
    void seek(const std::string& actor, double seconds) {
        AnimationPlaybackAccess::control(*animation, scene.reference(actor),
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
    }
    Json pose(const std::string& actor) {
        return animation->presentation(scene.entity(actor).id(), 1);
    }
};
Json reference_document(Json source, bool incoming) {
    for (auto& row : source.at("entities")) {
        auto& components = row.at("components");
        if (!components.contains("forge.animator"))
            continue;
        auto& config = components.at("forge.animator");
        if (incoming)
            config["clip"] = config.at("sequence_next_clip");
        config["sequence"] = nullptr;
        config["sequence_start_seconds"] = 0;
        config["sequence_next_clip"] = nullptr;
        config["sequence_next_start_seconds"] = 0;
        config["playback_speed"] = 1;
        config["loop"] = false;
    }
    return source;
}
Json exercise(const std::filesystem::path& root, AssetId id) {
    const auto source = load_game_scene(root, {id});
    std::string controller;
    std::string mapped_actor;
    std::vector<std::string> actors;
    for (const auto& row : source.at("entities")) {
        if (row.at("components").contains("forge.animation_sequence"))
            controller = row.at("id");
        if (row.at("components").contains("forge.animator")) {
            actors.push_back(row.at("id"));
            if (row.at("name").get_ref<const std::string&>().starts_with("Right"))
                mapped_actor = row.at("id");
        }
    }
    check(!controller.empty() && actors.size() == 3,
          "Gallery needs one controller and three original model tracks");
    World live(root, source), first(root, reference_document(source, false)),
        next(root, reference_document(source, true));
    Json observations = Json::array();
    bool morphs = false, mapped_motion = false;
    for (const double seconds : {0., .75, 1.25, 1.75, 2.5, 3.5}) {
        live.seek(controller, seconds);
        Json sample = Json::array();
        for (const auto& actor : actors) {
            const auto ref = live.scene.reference(actor);
            const auto config = live.scene.entity(actor).get<Animator>();
            const auto a_duration = first.animation->playback(ref)->duration;
            const auto b_duration = next.animation->playback(ref)->duration;
            const auto start = config.sequence_next_start_seconds;
            const auto a_time = std::clamp(seconds, 0., a_duration);
            const auto b_time = std::clamp(seconds - start, 0., b_duration);
            const auto weight = std::clamp((seconds - start) / (a_duration - start), 0., 1.);
            first.seek(actor, a_time);
            next.seek(actor, b_time);
            const auto a = first.pose(actor), b = next.pose(actor), actual = live.pose(actor);
            check(!actual.is_null() && actual.at("local").size() == a.at("local").size() &&
                      a.at("local").size() == b.at("local").size(),
                  "Gallery sequence lost a model pose");
            for (std::size_t joint = 0; joint < actual.at("local").size(); ++joint)
                for (const auto* channel : {"translation", "scale"})
                    for (unsigned lane = 0; lane < 3; ++lane)
                        near(actual.at("local")[joint].at(channel)[lane],
                             a.at("local")[joint].at(channel)[lane].get<double>() * (1 - weight) +
                                 b.at("local")[joint].at(channel)[lane].get<double>() * weight);
            check(actual.at("morphs").size() == a.at("morphs").size() &&
                      a.at("morphs").size() == b.at("morphs").size(),
                  "Sequence morph defaults or node channels changed");
            for (std::size_t node = 0; node < actual.at("morphs").size(); ++node) {
                check(actual.at("morphs")[node].at("node") == a.at("morphs")[node].at("node") &&
                          a.at("morphs")[node].at("node") == b.at("morphs")[node].at("node"),
                      "Sequence morph node identity changed");
                for (std::size_t lane = 0; lane < actual.at("morphs")[node].at("weights").size();
                     ++lane) {
                    const double av = a.at("morphs")[node].at("weights")[lane];
                    const double bv = b.at("morphs")[node].at("weights")[lane];
                    near(actual.at("morphs")[node].at("weights")[lane],
                         av * (1 - weight) + bv * weight);
                    morphs |=
                        seconds == 1.75 && weight > 0 && weight < 1 && std::abs(av - bv) > .01;
                }
            }
            const auto status = AnimationPlaybackAccess::status(*live.animation, ref);
            near(status.at("blend_weight"), weight);
            near(status.at("sequence_clips").at("duration_seconds"), a_duration);
            near(status.at("sequence_clips").at("next").at("duration_seconds"), b_duration);
            if (actor == mapped_actor && seconds == 1.75)
                mapped_motion = a.at("local") != b.at("local");
            sample.push_back({{"pose", actual}, {"playback", status}});
        }
        const auto checkpoint = live.animation->checkpoint();
        check(checkpoint.at("version") == 10, "Gallery omitted the skeletal recovery profile");
        World recovered(root, source);
        recovered.animation->restore(checkpoint);
        for (const auto& actor : actors)
            check(recovered.pose(actor) == live.pose(actor),
                  "Recovered Gallery changed a skeletal/morph/retarget sample");
        observations.push_back(std::move(sample));
    }
    check(morphs, "Gallery did not exercise overlapping morph channels");
    check(mapped_motion, "Gallery did not exercise mapped motion");
    return observations;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            std::cout << test::author_skeletal_sequence_gallery(argv[2]).str() << '\n';
            return 0;
        }
        check(argc == 3, "Need Gallery and scratch, or --author Gallery");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "SourceProject";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        AssetId id;
        if (std::filesystem::exists(root / test::skeletal_sequence_scene_path))
            id = read(root / test::skeletal_sequence_scene_path).at("asset_id").get<AssetId>();
        else
            id = test::author_skeletal_sequence_gallery(root);
        const auto saved = read(root / test::skeletal_sequence_scene_path);
        const auto expected = exercise(root, id);
        check(read(root / test::skeletal_sequence_scene_path) == saved,
              "Gallery execution changed its editable Scene source");
        const std::array roots{id};
        package_runtime_content(root, scratch / "Export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        check(!std::filesystem::exists(scratch / "Relocated/Assets/LocomotionColumn.gltf") &&
                  !std::filesystem::exists(scratch / "Relocated/Assets/OverlayColumn.gltf") &&
                  !std::filesystem::exists(
                      scratch / "Relocated/Assets/AnimationRetarget/Swing.retarget.json"),
              "Runtime export retained editable animation sources");
        check(exercise(scratch / "Relocated", id) == expected,
              "Source-hidden relocated skeletal Gallery samples changed");
        std::cout << "Original skeletal/morph/mapped tracks, independent phases, recovery and "
                     "source-hidden export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
