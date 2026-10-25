#include "editor/content_scan.hpp"
#include <fstream>
#include <iostream>

namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void write(const std::filesystem::path& path, const forge::Json& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << value.dump();
    file.close();
    require(bool(file), "Cannot write discovery fixture");
}
} // namespace

int main(int argc, char** argv) {
    using namespace forge;
    try {
        require(argc == 2, "Expected scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        auto scene = empty_scene();
        write(root / "Assets/Upper.SCENE.JSON", scene);
        write(root / ".forge/hidden.scene.json", empty_scene());
        write(root / ".git/hidden.scene.json", empty_scene());
        write(root / "Assets/custom.json", {{"version", 1}, {"entities", Json::array()}});
        write(root / "Assets/non-scene.json", {{"unrelated", true}});
        AssetCatalog indexed(root);
        const auto texture = AssetId::generate();
        indexed.add({texture, "texture", "Assets/image.png"});
        indexed.save(AssetCatalog::project_index(root));
        auto first = scan_content_catalog(root);
        require(first.diagnostic.empty() && first.catalog.records().size() == 2 &&
                    first.scenes->size() == 1 && scene_files(root).size() == 2,
                "Scene recognition, legacy listing or hidden-folder filtering changed");
        require(scene_files(std::filesystem::relative(root)).size() == 2,
                "Scene discovery rejected a relative project folder");
        const auto damaged_path = root / "Assets/incomplete.scene.json";
        const auto repair = empty_scene();
        for (const auto& tail : {std::string(1, '\0'), std::string("\0hidden", 7),
                                 std::string("\x1a hidden"), std::string(" {}")}) {
            atomic_write(damaged_path, repair.dump() + tail);
            const auto incomplete = scan_content_catalog(root, first.scenes);
            require(scene_files(root).size() == 2 && incomplete.diagnostic.empty() &&
                        incomplete.catalog.records().size() == first.catalog.records().size() &&
                        incomplete.scenes->size() == first.scenes->size(),
                    "Incomplete JSON source entered Content discovery");
        }
        write(damaged_path, repair);
        require(scene_files(root).size() == 3 && scan_content_catalog(root).scenes->size() == 2,
                "Repaired JSON source did not return to discovery");
        std::filesystem::remove(damaged_path);
        {
            std::ofstream oversized(root / "Assets/oversized.json");
            oversized.seekp(8 * 1024 * 1024);
            oversized.put(' ');
            oversized.close();
            require(bool(oversized), "Cannot write oversized source fixture");
        }
        std::error_code link_error;
        std::filesystem::create_symlink(root / "Assets/Upper.SCENE.JSON",
                                        root / "Assets/alias.scene.json", link_error);
        require(scene_files(root).size() == 2, "Oversized or linked input entered scene discovery");
        AssetCatalog explicit_registration(root);
        const auto registered = explicit_registration.add_scene("Assets/Upper.SCENE.JSON");
        require(registered.id == first.scenes->front().id &&
                    registered.schema_version == first.scenes->front().schema_version,
                "Discovery bypassed explicit scene registration's schema/identity admission");

        // A recognized but invalid scene must fail the candidate as a whole.
        auto broken = empty_scene();
        broken["entities"] = Json::array({Json::object()});
        write(root / "Assets/Broken.scene.json", broken);
        const auto another = AssetId::generate();
        indexed.add({another, "texture", "Assets/another.png"});
        indexed.save(AssetCatalog::project_index(root));
        auto failed = scan_content_catalog(root, first.scenes);
        require(!failed.diagnostic.empty() && failed.scenes == first.scenes &&
                    failed.catalog.records().contains(another) &&
                    failed.catalog.records().contains(registered.id),
                "Failed scan hid new registered assets or lost the previous complete scene list");
        std::filesystem::remove(root / "Assets/Broken.scene.json");
        write(root / "Assets/duplicate.scene.json", scene);
        failed = scan_content_catalog(root, first.scenes);
        require(!failed.diagnostic.empty() && failed.scenes == first.scenes,
                "Duplicate discovered identity was accepted or replaced the last good list");
        std::filesystem::remove(root / "Assets/duplicate.scene.json");

        // Non-scene registered sources are excluded even if their JSON resembles a scene.
        write(root / "Assets/registered.json", scene);
        indexed.add({AssetId::generate(), "material", "Assets/registered.json"});
        indexed.save(AssetCatalog::project_index(root));
        auto second = empty_scene();
        write(root / "Assets/a.scene.json", second);
        auto existing_scene = registered;
        existing_scene.metadata = {{"custom", "keep registered metadata"}};
        indexed.add(existing_scene);
        const std::vector<AssetDependency> edges{
            {texture, "texture", AssetDependencyKind::Runtime, "declared:preview", {}}};
        indexed.set_dependencies(registered.id, edges);
        indexed.save(AssetCatalog::project_index(root));
        const auto ordered = scan_content_catalog(root);
        require(ordered.diagnostic.empty() && ordered.scenes->size() == 2 &&
                    ordered.scenes->front().source == "Assets/a.scene.json",
                "Discovery exclusions or case-folded list ordering changed");
        require(ordered.catalog.records().at(registered.id).metadata == existing_scene.metadata &&
                    ordered.catalog.dependency_graph().dependencies(registered.id) == edges &&
                    ordered.catalog.records().contains(second.at("asset_id").get<AssetId>()),
                "Bulk discovery replaced registered metadata or dependency edges");
        indexed.add_scene("Assets/a.scene.json");
        indexed.save(AssetCatalog::project_index(root));
        require(scan_content_catalog(root).catalog.document() == indexed.document(),
                "Discovery changed an already complete registered catalog");
        const auto many = root / "Assets/Many";
        std::filesystem::create_directories(many);
        for (int i = 0; i < 10001; ++i) {
            std::ofstream file(many / (std::to_string(i) + ".txt"));
            require(bool(file), "Cannot write large-project fixture");
        }
        require(scan_content_catalog(root).diagnostic.empty(),
                "Unrelated sources hit the obsolete 10000-entry scene discovery cap");

        std::stop_source stop;
        stop.request_stop();
        bool cancelled = false;
        try {
            (void)scan_content_catalog(root, ordered.scenes, {}, stop.get_token());
        } catch (const std::exception&) {
            cancelled = true;
        }
        require(cancelled, "Cancelled discovery returned a publishable candidate");
        const auto empty = root / "Empty";
        std::filesystem::create_directory(empty);
        cancelled = false;
        try {
            (void)scene_files(empty, stop.get_token());
        } catch (const std::exception&) {
            cancelled = true;
        }
        require(cancelled, "An empty scene scan ignored cancellation");
        cancelled = false;
        try {
            (void)ui::cpp_project_sources(empty, stop.get_token());
        } catch (const std::exception&) {
            cancelled = true;
        }
        require(cancelled, "C++ source discovery ignored cancellation");

        // Unrelated directories must also count toward the traversal bounds.
        const auto cpp_root = root / "Cpp";
        auto deep = cpp_root / "Native";
        const SourceScanOptions bounds;
        for (std::size_t i = 0; i <= bounds.max_depth; ++i)
            deep /= "d";
        std::filesystem::create_directories(deep);
        const std::vector<std::string> previous_code{"Native/Existing.cpp"};
        const auto limited = scan_content_catalog(cpp_root, {}, previous_code);
        require(
            limited.diagnostic.find("directory levels") != std::string::npos &&
                limited.code_sources == previous_code && limited.scenes,
            "Overdeep C++ scan published a partial list or blocked independent scene discovery");

        // The callback observes the parsed snapshot, even if the file changes
        // immediately afterward. Registration must not parse different bytes.
        const auto isolated = root / "Snapshot";
        write(isolated / "one.scene.json", scene);
        AssetCatalog snapshot(isolated);
        int observed = 0;
        (void)scene_files(isolated, {}, {}, [&](const auto& path, const Json& parsed) {
            ++observed;
            write(isolated / path, {{"external", "replacement"}});
            snapshot.add(detail::scene_asset_record(path, parsed));
        });
        require(observed == 1 && snapshot.records().contains(registered.id),
                "Discovery reread a changed file or lost its admitted snapshot");
        std::cout << "Portable Content discovery tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
