#include "fixtures/prefab_nested_gallery.hpp"
#include "fixtures/prefab_source_layers_gallery.hpp"
#include "runtime_package.hpp"
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        using namespace test_prefab_nested_gallery;
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--source-layers"),
              "Provide Gallery and isolated output directories, optionally --source-layers");
        const bool layers = argc == 4;
        const auto* scene_path = layers ? test_prefab_source_layers_gallery::scene_path
                                        : test_prefab_nested_gallery::scene_path;
        const auto* scene_uuid = layers ? test_prefab_source_layers_gallery::scene_uuid
                                        : test_prefab_nested_gallery::scene_uuid;
        const auto author =
            layers ? test_prefab_source_layers_gallery::author : test_prefab_nested_gallery::author;
        const auto verify =
            layers ? test_prefab_source_layers_gallery::verify : test_prefab_nested_gallery::verify;
        const auto output = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = output / "source", package = output / "export",
                   moved = output / "relocated";
        std::filesystem::create_directories(output);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(source / scene_path))
            author(source);
        verify(source);
        const std::array scenes{AssetId::parse(scene_uuid)};
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, scenes, {});
        }
        package_runtime_content(source, package, scenes, {"windows", "d3d12"});
        std::filesystem::rename(package, moved);
        std::filesystem::rename(source, output / "source-hidden");
        verify(moved);
        std::cout << "Prefab composition source admission, runtime consumers and relocated "
                     "source-hidden Windows-target content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
