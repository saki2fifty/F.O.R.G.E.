#include "asset_storage.hpp"
#include "editor/content_model.hpp"
#include "ui_asset_catalog.hpp"
#include <forge/flecs_script.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid UI catalog operation accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected fixture directory and font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{root};
        ProjectLease writer(root);
        const std::string rml = "<rml><body><p>HUD</p></body></rml>";
        asset_storage::replace(root / "Assets/hud.rml", rml);
        asset_storage::replace(root / "Assets/hud.rcss", "body {color:red;}");
        std::filesystem::copy_file(argv[2], root / "Assets/font.ttf");
        auto document = register_ui_source(writer, "Assets/hud.rml");
        auto font = register_ui_source(writer, "Assets/font.ttf");
        check(document.type == "ui_document" && font.type == "ui_font" &&
                  register_ui_source(writer, "Assets/font.ttf").id == font.id,
              "UI source identity/type");
        UiResources admitted(root);
        admitted.document({document.id});
        admitted.read("Assets/hud.rcss");
        admitted.read("Assets/font.ttf");
        auto snapshot = admitted.snapshot();
        auto catalog = refresh_ui_asset_catalog(writer, snapshot);
        check(catalog.records().size() == 3 &&
                  catalog.records().at(document.id).dependency_edges.size() == 2,
              "UI source dependencies absent");
        check(catalog.dependency_graph().referrers(font.id) == std::vector<AssetId>{document.id},
              "Font referrer missing");
        check(catalog.dependency_graph().source_referrers("Assets/hud.rcss").size() == 2,
              "UI source invalidation index");
        SourceSnapshot sources;
        for (const auto& s : snapshot.sources)
            sources.files[s.source] = {s.source, s.digest, s.bytes, {}, "ui", {}};
        auto before = ContentIndex::build(catalog, &sources);
        ContentQuery changed;
        changed.state = ContentState::Changed;
        check(before.query(changed).empty(), "Fresh UI sources reported changed");
        sources.files["Assets/font.ttf"].digest = std::string(64, '0');
        check(ContentIndex::build(catalog, &sources).query(changed).size() == 2,
              "UI font change not reflected in shared Content status");
        const auto index = asset_storage::read(root / "forge.assets.json");
        auto bad = snapshot;
        bad.documents[document.id] = "Assets/hud.rcss";
        rejects([&] { refresh_ui_asset_catalog(writer, bad); });
        bad = snapshot;
        bad.sources.push_back(bad.sources.front());
        rejects([&] { refresh_ui_asset_catalog(writer, bad); });
        asset_storage::replace(root / "Assets/hud.rcss", "body {color:blue;}");
        rejects([&] { refresh_ui_asset_catalog(writer, snapshot); });
        asset_storage::replace(root / "Assets/font.ttf", "bad font");
        rejects([&] { register_ui_source(writer, "Assets/font.ttf"); });
        rejects([&] { register_ui_source(writer, "../escape.rml"); });
        check(asset_storage::read(root / "forge.assets.json") == index,
              "Rejected UI source changed prior catalog");
        std::vector<std::byte> tga(18 + 3, std::byte{});
        tga[2] = std::byte{2};
        tga[12] = tga[14] = std::byte{1};
        tga[16] = std::byte{24};
        tga[17] = std::byte{32};
        std::ofstream image(root / "Assets/image.tga", std::ios::binary);
        image.write(reinterpret_cast<const char*>(tga.data()), tga.size());
        image.close();
        auto texture = register_ui_source(writer, "Assets/image.tga");
        check(texture.type == "texture", "UI image invented a separate Texture identity type");
        catalog = AssetCatalog::open_project(root);
        texture.metadata["opaque.extension"] = {{"unchanged", true}};
        catalog.replace(texture);
        catalog.save(root / "forge.assets.json");
        const auto refreshed = register_ui_source(writer, "Assets/image.tga");
        check(refreshed.id == texture.id &&
                  refreshed.metadata.at("opaque.extension") ==
                      texture.metadata.at("opaque.extension") &&
                  refreshed.metadata.at("forge.ui_source").at("width") == 1,
              "UI image refresh lost Texture identity, extension data or dimensions");
        asset_storage::replace(root / "Assets/source.flecs", "Example {}\n");
        const auto script = register_flecs_script(root, "Assets/source.flecs");
        asset_storage::replace(root / "Assets/source.flecs", "Changed {}\n");
        const auto script_updated = register_flecs_script(root, "Assets/source.flecs");
        check(script.id == script_updated.id && script.source_dependencies.size() == 1 &&
                  script.source_dependencies[0].revision !=
                      script_updated.source_dependencies[0].revision,
              "Script root-source observation or stable identity was lost");
        std::cout << "UI catalog identity, admission, source status, graph and stale preservation "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
