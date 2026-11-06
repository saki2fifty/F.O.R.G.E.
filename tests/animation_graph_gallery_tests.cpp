#include "animation_graph_resource.hpp"
#include "fixtures/animation_graph_gallery.hpp"
#include <forge/animation.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json sample(const std::filesystem::path& root, const Json& source, float seconds) {
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        require(std::chrono::steady_clock::now() < deadline,
                "Animation Graph Gallery admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (seconds > 0)
        animation->tick(seconds, 1);
    Json results = Json::array();
    for (const auto& row : source.at("entities")) {
        if (!row.at("components").contains("forge.animator"))
            continue;
        const auto entity = scene.entity(row.at("id").get<std::string>());
        auto pose = animation->presentation(entity.id(), 1);
        require(pose.contains("graph") && pose.at("graph_inputs").size() == 2,
                "Graph Gallery lost its named input projection");
        results.push_back({{"name", row.at("name")}, {"pose", std::move(pose)}});
    }
    require(results.size() == 3, "Graph Gallery needs three comparison instances");
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.at("name") < b.at("name"); });
    return results;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            std::cout
                << test::author_animation_graph_gallery(std::filesystem::absolute(argv[2])).str()
                << '\n';
            return 0;
        }
        require(argc == 3, "Need Gallery and scratch, or --author Gallery");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "SourceProject";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(root / test::animation_graph_scene_path) >> source;
        const auto first = sample(root, source, 0), moving = sample(root, source, .5f);
        for (unsigned i = 0; i < 2; ++i)
            require(first[i].at("pose").at("local") != moving[i].at("pose").at("local"),
                    "Graph Gallery default/full-motion instance did not animate");
        require(first[2].at("pose").at("local") == moving[2].at("pose").at("local") &&
                    moving[0].at("pose").at("local") != moving[1].at("pose").at("local"),
                "Graph Gallery overrides did not distinguish gentle/full/rest poses");
        const auto catalog = AssetCatalog::open_project(root);
        const auto graph = moving[0].at("pose").at("graph").get<AssetId>();
        const auto& record = catalog.records().at(graph);
        const auto artifact = DerivedDataCache::open_existing(root).load_selected(
            record.metadata.at("forge.import").at("key").get<std::string>(), [](const auto&) {});
        const auto admitted = admit_animation_graph(artifact, record);
        require(admitted.graph.nodes.size() == 4 && admitted.clips.size() == 2 &&
                    admitted.masks.size() == 1,
                "Graph Gallery lost a shared path, imported/baked clip or mask");
        const std::array roots{source.at("asset_id").get<AssetId>()};
        package_runtime_content(root, scratch / "Export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        require(!std::filesystem::exists(scratch / "Relocated" / test::animation_graph_source_path),
                "Graph runtime package retained authored source");
        require(first == sample(scratch / "Relocated", source, 0) &&
                    moving == sample(scratch / "Relocated", source, .5f),
                "Graph Gallery poses changed after source-hidden relocation");
        std::cout << "Graph Gallery shared paths, imported/baked clips, masks, overrides and "
                     "relocated poses passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
