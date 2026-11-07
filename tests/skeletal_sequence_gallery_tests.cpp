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
        config["sequence_extra_clips"] = Json::array();
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
        if (row.at("components").contains("forge.animation_sequence") &&
            row.at("components").at("forge.animation_sequence").value("sequence", Json()).is_null())
            controller = row.at("id");
        if (row.at("components").contains("forge.animator")) {
            actors.push_back(row.at("id"));
            if (row.at("name").get_ref<const std::string&>().starts_with("Right"))
                mapped_actor = row.at("id");
        }
    }
    check(!controller.empty() && actors.size() == 3,
          "Gallery needs one controller and three original model tracks");
    const bool rates = std::any_of(
        source.at("entities").begin(), source.at("entities").end(), [](const Json& row) {
            const auto& components = row.at("components");
            return components.contains("forge.animation_sequence") &&
                   components.at("forge.animation_sequence").value("sequence_rate", 1.) != 1;
        });
    const bool collections = std::any_of(
        source.at("entities").begin(), source.at("entities").end(), [](const Json& row) {
            const auto& components = row.at("components");
            return components.contains("forge.animator") &&
                   !components.at("forge.animator")
                        .value("sequence_extra_clips", Json::array())
                        .empty();
        });
    const bool child_loops = std::any_of(
        source.at("entities").begin(), source.at("entities").end(), [](const Json& row) {
            const auto& components = row.at("components");
            return components.contains("forge.animation_sequence") &&
                   components.at("forge.animation_sequence").value("sequence_loop", false);
        });
    World live(root, source), first(root, reference_document(source, false)),
        next(root, reference_document(source, true));
    Json observations = Json::array();
    bool morphs = false, mapped_motion = false;
    const std::vector<double> sample_times =
        child_loops ? std::vector<double>{0,    .5,  .625, .99,  1,   1.25, 1.5, 2.5,
                                          2.75, 3.5, 4.75, 5.25, 6.5, 6.75, 7.75}
        : collections
            ? std::vector<double>{0, .75, 1.25, 1.75, 2.5, 3.5, 5.25, 7.75, 10.25, 14.75, 15.5}
            : std::vector<double>{0, .75, 1.25, 1.75, 2.5, 3.5};
    for (const double seconds : sample_times) {
        live.seek(controller, seconds);
        Json sample = Json::array();
        for (const auto& actor : actors) {
            const auto ref = live.scene.reference(actor);
            const auto config = live.scene.entity(actor).get<Animator>();
            const auto a_duration = first.animation->playback(ref)->duration;
            const auto b_duration = next.animation->playback(ref)->duration;
            const auto start = config.sequence_next_start_seconds;
            // Build the reference mapping from saved values, independently of the
            // runtime provider, then compare native standalone clip samples.
            std::vector<AnimationSequence> chain;
            auto owner = config.sequence;
            for (;;) {
                const auto sequence =
                    live.scene.entity(owner.entity.str()).get<AnimationSequence>();
                chain.push_back(sequence);
                if (!sequence.sequence.entity)
                    break;
                owner = sequence.sequence;
                check(chain.size() < 64, "Gallery reference chain is cyclic");
            }
            double local = seconds, rate = 1, parent_duration = chain.back().duration;
            for (auto child = chain.rbegin() + 1; child != chain.rend(); ++child) {
                const auto unwrapped =
                    std::max(0., (local - child->sequence_start_seconds) * child->sequence_rate);
                if (child->sequence_loop && unwrapped > 0) {
                    const bool parent_end = local == parent_duration;
                    local = std::fmod(unwrapped, child->duration);
                    if (parent_end && local == 0)
                        local = child->duration;
                } else
                    local = std::min(unwrapped, child->duration);
                parent_duration = child->duration;
                rate *= child->sequence_rate;
            }
            std::size_t active = 0;
            double a_start = config.sequence_start_seconds, b_start = start;
            if (collections) {
                check(config.sequence_extra_clips.size() == 6,
                      "Collection Gallery needs eight placements per model");
                const auto placed_start = [&](std::size_t i) {
                    return i == 0   ? config.sequence_start_seconds
                           : i == 1 ? start
                                    : config.sequence_extra_clips[i - 2].start_seconds;
                };
                while (active + 2 < sequence_clip_limit &&
                       local >=
                           std::max(placed_start(active) + (active % 2 ? b_duration : a_duration),
                                    placed_start(active + 1)))
                    ++active;
                a_start = placed_start(active);
                b_start = placed_start(active + 1);
            }
            const auto first_duration = active % 2 ? b_duration : a_duration;
            const auto next_duration = active % 2 ? a_duration : b_duration;
            const auto a_time = std::clamp(local - a_start, 0., first_duration);
            const auto b_time = std::clamp(local - b_start, 0., next_duration);
            const auto weight =
                b_start >= a_start + first_duration
                    ? (local >= b_start ? 1. : 0.)
                    : std::clamp((local - b_start) / (a_start + first_duration - b_start), 0., 1.);
            auto& a_world = active % 2 ? next : first;
            auto& b_world = active % 2 ? first : next;
            a_world.seek(actor, a_time);
            b_world.seek(actor, b_time);
            const auto a = a_world.pose(actor), b = b_world.pose(actor), actual = live.pose(actor);
            check(!actual.is_null() && actual.at("local").size() == a.at("local").size() &&
                      a.at("local").size() == b.at("local").size(),
                  "Gallery sequence lost a model pose");
            for (std::size_t joint = 0; joint < actual.at("local").size(); ++joint)
                for (const auto* channel : {"translation", "scale"})
                    for (unsigned lane = 0; lane < 3; ++lane)
                        near(actual.at("local")[joint].at(channel)[lane],
                             a.at("local")[joint].at(channel)[lane].get<double>() * (1 - weight) +
                                 b.at("local")[joint].at(channel)[lane].get<double>() * weight);
            for (std::size_t joint = 0; joint < actual.at("local").size(); ++joint) {
                const auto& qa = a.at("local")[joint].at("rotation");
                const auto& qb = b.at("local")[joint].at("rotation");
                double dot = 0;
                for (unsigned lane = 0; lane < 4; ++lane)
                    dot += qa[lane].get<double>() * qb[lane].get<double>();
                std::array<double, 4> expected;
                double length_squared = 0;
                for (unsigned lane = 0; lane < 4; ++lane) {
                    expected[lane] = qa[lane].get<double>() * (1 - weight) +
                                     qb[lane].get<double>() * weight * (dot < 0 ? -1 : 1);
                    length_squared += expected[lane] * expected[lane];
                }
                check(length_squared > 0, "Reference skeletal rotation is degenerate");
                double agreement = 0;
                for (unsigned lane = 0; lane < 4; ++lane)
                    agreement += actual.at("local")[joint].at("rotation")[lane].get<double>() *
                                 expected[lane] / std::sqrt(length_squared);
                near(std::abs(agreement), 1);
            }
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
                    morphs |= weight > 0 && weight < 1 && std::abs(av - bv) > .01;
                }
            }
            const auto status = AnimationPlaybackAccess::status(*live.animation, ref);
            near(status.at("speed"), rate);
            near(status.at("blend_weight"), weight);
            near(status.at("sequence_clips").at("duration_seconds"), first_duration);
            near(status.at("sequence_clips").at("next").at("duration_seconds"), next_duration);
            if (collections)
                check(status.at("sequence_clips").at("active_primary") == active &&
                          status.at("sequence_clips").at("clips").size() == 8,
                      "Collection Gallery reported another active pair or list");
            if (actor == mapped_actor && weight > 0)
                mapped_motion |= a.at("local") != b.at("local");
            sample.push_back({{"pose", actual}, {"playback", status}});
        }
        const auto checkpoint = live.animation->checkpoint();
        check(checkpoint.at("version") == (child_loops   ? 13
                                           : collections ? 12
                                           : rates       ? 11
                                                         : 10),
              "Gallery omitted the skeletal/rate recovery profile");
        if (child_loops)
            check(checkpoint.at("sequence_loop_profile") == 11 &&
                      checkpoint.at("sequence_rate_profile") == 10 &&
                      checkpoint.at("skeletal_sequence_profile") == 7,
                  "Loop Gallery lost its nested/rate/skeletal recovery composition");
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
        if (argc == 3 && (std::string_view(argv[1]) == "--author" ||
                          std::string_view(argv[1]) == "--author-rates" ||
                          std::string_view(argv[1]) == "--author-collections" ||
                          std::string_view(argv[1]) == "--author-child-loops")) {
            std::cout << test::author_skeletal_sequence_gallery(
                             argv[2], std::string_view(argv[1]) == "--author-rates",
                             std::string_view(argv[1]) == "--author-collections",
                             std::string_view(argv[1]) == "--author-child-loops")
                             .str()
                      << '\n';
            return 0;
        }
        const bool rates = argc == 4 && std::string_view(argv[1]) == "--rates";
        const bool collections = argc == 4 && std::string_view(argv[1]) == "--collections";
        const bool child_loops = argc == 4 && std::string_view(argv[1]) == "--child-loops";
        check(argc == 3 || rates || collections || child_loops,
              "Need [--rates|--collections|--child-loops] Gallery and scratch, or "
              "--author[-rates|-collections|-child-loops] "
              "Gallery");
        const bool selected = rates || collections || child_loops;
        const auto scratch =
            std::filesystem::absolute(argv[selected ? 3 : 2]) / AssetId::generate().str();
        const auto root = scratch / "SourceProject";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[selected ? 2 : 1], root,
                              std::filesystem::copy_options::recursive);
        const auto path = child_loops   ? test::sequence_child_loop_scene_path
                          : collections ? test::sequence_collection_scene_path
                          : rates       ? test::sequence_rate_scene_path
                                        : test::skeletal_sequence_scene_path;
        AssetId id;
        if (std::filesystem::exists(root / path))
            id = read(root / path).at("asset_id").get<AssetId>();
        else
            id = test::author_skeletal_sequence_gallery(root, rates, collections, child_loops);
        const auto saved = read(root / path);
        const auto expected = exercise(root, id);
        check(read(root / path) == saved, "Gallery execution changed its editable Scene source");
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
