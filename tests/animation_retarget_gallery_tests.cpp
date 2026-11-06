#include "animation_clip_selection.hpp"
#include "animation_resource.hpp"
#include "fixtures/animation_retarget_gallery.hpp"
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json sample(const std::filesystem::path& root, const Json& authored, float time) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(authored);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        require(std::chrono::steady_clock::now() < deadline, "Retarget Gallery loading timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (time > 0)
        animation->tick(time, 1);
    Json result = Json::array();
    for (const auto& row : authored.at("entities")) {
        if (!row.at("components").contains("forge.animator"))
            continue;
        const auto actor = scene.entity(row.at("id").get<std::string>());
        const auto config = actor.get<Animator>();
        const auto pose = animation->presentation(actor.id(), 1);
        result.push_back(pose);
        const auto catalog = AssetCatalog::open_project(root);
        const auto binding =
            animation_detail::select_animation_clip(catalog, config.skeleton, config.clip);
        if (!binding.baked)
            continue;
        const auto model = asset_detail::load_model_selection(root, catalog, binding.model);
        animation_detail::Skeleton skeleton(model.bytes(model.member(config.skeleton.id)));
        for (std::size_t j = 0; j < pose.at("local").size(); ++j) {
            const auto rest = skeleton.joint_rest_pose(j);
            for (unsigned c = 0; c < 3; ++c)
                require(std::abs(pose.at("local")[j].at("translation")[c].get<double>() -
                                 rest.translation[c]) < .004,
                        "Retarget Gallery lost target rest proportions");
        }
    }
    require(result.size() == 2, "Retarget Gallery needs source and target Animators");
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--author") {
            std::cout << test::author_animation_retarget_gallery(std::filesystem::absolute(argv[2]),
                                                                 std::filesystem::absolute(argv[3]),
                                                                 std::filesystem::absolute(argv[4]))
                             .str()
                      << '\n';
            return 0;
        }
        require(argc == 3, "Need Gallery and scratch, or --author Gallery worker converter");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "SourceProject";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json authored;
        std::ifstream(root / test::retarget_scene_path) >> authored;
        const auto first = sample(root, authored, 0);
        const auto moving = sample(root, authored, .5);
        for (unsigned i = 0; i < 2; ++i)
            require(first[i].at("local") != moving[i].at("local"),
                    "Retarget Gallery source or target did not animate");
        const std::array roots{authored.at("asset_id").get<AssetId>()};
        package_runtime_content(root, scratch / "Export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        require(!std::filesystem::exists(scratch / "Relocated" / test::retarget_recipe_path),
                "Runtime package retained editable retarget source");
        require(first == sample(scratch / "Relocated", authored, 0) &&
                    moving == sample(scratch / "Relocated", authored, .5),
                "Retarget Gallery pose changed after source-hidden relocation");
        std::cout << "Both original rigs animate; target proportions and relocated poses match\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
