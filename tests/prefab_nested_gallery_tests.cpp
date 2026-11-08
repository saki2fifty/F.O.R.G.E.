#include "fixtures/prefab_nested_gallery.hpp"
#include "runtime_package.hpp"
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        using namespace test_prefab_nested_gallery;
        check(argc == 3, "Provide Gallery and isolated output directories");
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
        std::cout << "Nested assembly source admission, runtime consumers and relocated "
                     "source-hidden Windows-target content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
