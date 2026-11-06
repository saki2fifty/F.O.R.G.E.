#include "animation_asset.hpp"
#include "asset_bytes.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace forge::animation_detail;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double actual, double expected) {
    check(std::isfinite(actual) && std::abs(actual - expected) < .003,
          "Graph pose differs from the analytic two-joint trajectory");
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected native graph sampling rejection");
}
AnimationPoseNode leaf(Sampler& sampler) {
    AnimationPoseNode result;
    result.sampler = &sampler;
    return result;
}
AnimationPoseNode mix(unsigned first, unsigned second, float weight, bool additive = false,
                      std::span<const float> mask = {}) {
    AnimationPoseNode result;
    result.first = first;
    result.second = second;
    result.weight = weight;
    result.additive = additive;
    result.joint_mask = mask;
    return result;
}
void same(std::span<const Matrix> a, std::span<const Matrix> b) {
    check(a.size() == b.size(), "Graph changed model joint count");
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t scalar = 0; scalar < a[i].size(); ++scalar)
            near(a[i][scalar], b[i][scalar]);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need admitted original skeleton, lift and rotation clips");
        const auto bytes = forge::asset_detail::read_bytes(argv[1], max_archive_bytes);
        auto skeleton = std::make_shared<Skeleton>(bytes);
        auto lift_clip =
            std::make_shared<Clip>(forge::asset_detail::read_bytes(argv[2], max_archive_bytes));
        auto turn_clip =
            std::make_shared<Clip>(forge::asset_detail::read_bytes(argv[3], max_archive_bytes));
        Sampler lift(skeleton, lift_clip), turn(skeleton, turn_clip);
        Sampler first_reference(skeleton, lift_clip), second_reference(skeleton, turn_clip);
        const std::array<float, 2> all{1, 1}, child{0, 1}, zero{0, 0};
        for (const auto additive : {false, true})
            for (const auto& mask : {std::span<const float>{}, std::span<const float>(all),
                                     std::span<const float>(child), std::span<const float>(zero)})
                for (const auto ratio : {0.f, .5f, 1.f, .2f})
                    for (const auto weight : {0.f, .5f, 1.f}) {
                        const std::array nodes{leaf(lift), leaf(turn),
                                               mix(0, 1, weight, additive, mask)};
                        same(lift.sample_graph(nodes, ratio),
                             first_reference.sample_blended(ratio, second_reference, weight, mask,
                                                            additive));
                    }
        // Lift gives the child Y=1.5 at phase1/2; Turn gives the root45 degrees.
        // Their half blend yields child1.25 and root22.5 degrees. Adding half
        // Lift's rest-relative offset raises the child to1.5 without rotating it.
        std::vector<AnimationPoseNode> graph{leaf(lift), leaf(turn), mix(0, 1, .5f), leaf(lift),
                                             mix(2, 3, .5f, true)};
        const auto before_lift = lift.sampling_jobs(), before_turn = turn.sampling_jobs();
        auto pose = lift.sample_graph(graph, .5f);
        const double angle = std::numbers::pi / 8;
        near(pose[1][12], -1.5 * std::sin(angle));
        near(pose[1][13], 1.5 * std::cos(angle));
        near(pose[0][0], std::cos(angle));
        check(lift.sampling_jobs() == before_lift + 1 && turn.sampling_jobs() == before_turn + 1,
              "A shared graph leaf performed repeated native sampling");
        graph[2].joint_mask = child;
        graph[4].joint_mask = child;
        pose = lift.sample_graph(graph, .5f);
        near(pose[1][12], 0);
        near(pose[1][13], 1.5);
        graph[2].joint_mask = {};
        graph[4].joint_mask = {};
        lift.root_motion_joint(0);
        pose = lift.sample_graph(graph, .5f);
        const auto root = lift.sampled_root();
        check(bool(root), "Graph output did not capture its composed root before removal");
        near(root->rotation[2], std::sin(angle / 2));
        near(root->rotation[3], std::cos(angle / 2));
        // Interval extraction needs each raw leaf, not the composed output root.
        near(lift.graph_joint_pose(0, 0).rotation[2], 0);
        near(lift.graph_joint_pose(1, 0).rotation[2], std::sin(std::numbers::pi / 8));
        near(lift.graph_joint_pose(4, 0).rotation[2], std::sin(angle / 2));
        near(pose[0][0], 1);
        near(pose[1][12], 0);
        near(pose[1][13], 1.5);
        lift.root_motion_joint({});
        rejects([&] { (void)lift.graph_joint_pose(100, 0); });
        rejects([&] { (void)lift.graph_joint_pose(0, 100); });
        (void)lift.sample(.5f);
        rejects([&] { (void)lift.graph_joint_pose(0, 0); });
        graph = {leaf(lift)};
        for (unsigned i = 1; i < 32; ++i)
            graph.push_back(mix(i - 1, 0, .5f));
        same(lift.sample_graph(graph, .5f), first_reference.sample(.5f));
        const auto count = lift.sampling_jobs();
        const auto bad = [&](auto edit) {
            auto invalid = graph;
            edit(invalid);
            rejects([&] { (void)lift.sample_graph(invalid, .5f); });
            check(lift.sampling_jobs() == count,
                  "Invalid native graph started sampling before complete preflight");
        };
        bad([](auto& nodes) { nodes.clear(); });
        bad([](auto& nodes) { nodes.push_back(mix(0, 1, .5f)); });
        bad([](auto& nodes) { nodes[1].first = 1; });
        bad([](auto& nodes) { nodes[1].second = UINT32_MAX; });
        bad([](auto& nodes) { nodes[1].weight = std::numeric_limits<float>::quiet_NaN(); });
        bad([](auto& nodes) { nodes[1].weight = 1.1f; });
        bad([](auto& nodes) { nodes[0].first = 0; });
        bad([](auto& nodes) { nodes[0].additive = true; });
        bad([&](auto& nodes) { nodes[0].joint_mask = child; });
        const std::array<float, 1> short_mask{1};
        bad([&](auto& nodes) { nodes[1].joint_mask = short_mask; });
        const std::array<float, 2> invalid_mask{0, std::numeric_limits<float>::infinity()};
        bad([&](auto& nodes) { nodes[1].joint_mask = invalid_mask; });
        bad([&](auto& nodes) { nodes.assign(9, leaf(lift)); });
        auto other_skeleton = std::make_shared<Skeleton>(bytes);
        Sampler foreign(other_skeleton, lift_clip);
        bad([&](auto& nodes) { nodes[0] = leaf(foreign); });
        rejects([&] { (void)lift.sample_graph(graph, std::numeric_limits<float>::quiet_NaN()); });
        rejects([&] { (void)lift.sample_graph(graph, -1); });
        rejects([&] { (void)lift.sample_graph(graph, 1.1f); });
        std::cout << "Native animation graph analytic/blend/additive/mask/shared-leaf/root/bounds "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
