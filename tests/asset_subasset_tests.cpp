#include <algorithm>
#include <forge/assets.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <set>

using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::string_view expected) {
    try {
        fn();
    } catch (const std::exception& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos,
                "Unexpected rejection diagnostic");
        return;
    }
    throw std::runtime_error("Invalid subasset candidate accepted");
}
std::vector<AssetRecord> rows(const AssetCatalog& catalog) {
    std::vector<AssetRecord> result;
    for (const auto& [id, record] : catalog.records()) {
        (void)id;
        result.push_back(record);
    }
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        atomic_write(root / "Assets/model.gltf", R"({"asset":{"version":"2.0"}})");
        const auto model_id = AssetId::generate(), mesh_id = AssetId::generate(),
                   material_id = AssetId::generate();
        AssetRecord model{model_id, "model", "Assets/model.gltf", 1, {}};
        AssetRecord mesh{mesh_id, "mesh", model.source, 1, {}};
        mesh.subasset = AssetSubasset{model_id, "mesh:durable-entry", false};
        mesh.metadata = {{"display_name", "Geometry"}, {"opaque", {{"plugin", "keep verbatim"}}}};
        AssetRecord material{material_id, "material", model.source, 1, {}};
        material.subasset = AssetSubasset{model_id, "material:durable-entry", false};
        AssetCatalog catalog(root);
        catalog.replace_all({mesh, material, model}); // Registration order is not identity.
        const auto member_ids = catalog.members(model_id);
        require(std::set<AssetId>(member_ids.begin(), member_ids.end()) ==
                    std::set<AssetId>{mesh_id, material_id},
                "Container member index incomplete");
        const AssetId roots[]{model_id};
        const auto order = catalog.dependency_graph().build_order(roots);
        require(order.size() == 3 && order.back() == model_id,
                "Subassets absent from build closure");
        require(catalog.dependency_graph().referrers(mesh_id) == std::vector<AssetId>{model_id},
                "Container relation not in shared graph");
        require(catalog.dependency_graph().source_referrers(model.source).size() == 3,
                "Shared source did not index all logical assets");
        const auto original = catalog.dependency_graph().document();
        auto reordered = rows(catalog);
        std::reverse(reordered.begin(), reordered.end());
        catalog.replace_all(reordered);
        require(catalog.dependency_graph().document() == original,
                "Reorder changed identity/graph");
        auto renamed = catalog.records().at(mesh_id);
        renamed.metadata["display_name"] = "Renamed Geometry";
        catalog.replace(renamed);
        require(catalog.records().at(mesh_id).subasset == mesh.subasset &&
                    catalog.records().at(mesh_id).metadata.at("opaque") ==
                        mesh.metadata.at("opaque"),
                "Display rename altered durable identity or opaque data");
        auto remapped = renamed;
        remapped.subasset->key = "different-entry";
        rejects([&] { catalog.replace(remapped); }, "preserve identity");
        auto wrong_type = renamed;
        wrong_type.type = "texture";
        auto bad_rows = rows(catalog);
        for (auto& row : bad_rows)
            if (row.id == mesh_id)
                row = wrong_type;
        rejects([&] { catalog.replace_all(bad_rows); }, "preserve identity");

        auto orphan = material;
        orphan.id = AssetId::generate();
        orphan.subasset->owner = AssetId::generate();
        orphan.source = "Assets/absent.gltf";
        rejects([&] { catalog.add(orphan); }, "registered root asset container");
        auto duplicate = mesh;
        duplicate.id = AssetId::generate();
        rejects([&] { catalog.add(duplicate); }, "mapping key");
        auto another = model;
        another.id = AssetId::generate();
        rejects([&] { catalog.add(another); }, "different identity");
        auto separate_source = material;
        separate_source.id = AssetId::generate();
        separate_source.subasset->key = "different-material";
        separate_source.source = "Assets/other.gltf";
        rejects([&] { catalog.add(separate_source); }, "canonical source locator");
        auto nested = material;
        nested.id = AssetId::generate();
        nested.subasset->owner = mesh_id;
        nested.source = "Assets/nested.gltf";
        rejects([&] { catalog.add(nested); }, "registered root asset container");
        require(catalog.records().size() == 3 && catalog.dependency_graph().document() == original,
                "Rejected candidate changed published family");

        auto removed = renamed;
        removed.subasset->removed = true;
        catalog.replace(removed);
        require(catalog.resolve(mesh_id, "mesh").state == AssetState::Removed,
                "Removed subasset reported available");
        require(catalog.members(model_id).size() == 1 &&
                    catalog.members(model_id, true).size() == 2,
                "Tombstone disappeared or remained in active members");
        require(catalog.dependency_graph().build_order(roots).size() == 2,
                "Removed output blocked active container build");
        rejects([&] { catalog.add(duplicate); }, "mapping key");
        const auto index = root / "forge.assets.json";
        catalog.save(index);
        auto reopened = AssetCatalog::open_project(root);
        require(reopened.records().at(mesh_id).subasset == removed.subasset &&
                    reopened.records().at(mesh_id).metadata == removed.metadata &&
                    reopened.resolve(mesh_id, "mesh").state == AssetState::Removed,
                "Tombstone identity/payload lost on reopen");
        reopened.replace(renamed); // Explicit compatible mapping restores the same logical member.
        require(reopened.resolve(mesh_id, "mesh").state == AssetState::Available,
                "Compatible member restoration lost its identity");
        std::filesystem::rename(root / model.source, root / "Assets/renamed.gltf");
        reopened.relocate(model_id, "Assets/renamed.gltf");
        for (const auto& [id, row] : reopened.records()) {
            (void)id;
            require(row.source == "Assets/renamed.gltf", "Container move left old child locator");
        }
        require(reopened.dependency_graph().source_referrers(model.source).empty() &&
                    reopened.dependency_graph().source_referrers("Assets/renamed.gltf").size() == 3,
                "Container relocation left stale primary source edges");
        rejects([&] { reopened.relocate(mesh_id, "Assets/independent.gltf"); }, "source container");

        // A new logical copy has new root/member IDs, even though its bytes match.
        std::filesystem::copy_file(root / "Assets/renamed.gltf", root / "Assets/copied.gltf");
        auto copied_model = model;
        copied_model.id = AssetId::generate();
        copied_model.source = "Assets/copied.gltf";
        auto copied_mesh = mesh;
        copied_mesh.id = AssetId::generate();
        copied_mesh.source = copied_model.source;
        copied_mesh.subasset->owner = copied_model.id;
        auto combined = rows(reopened);
        combined.push_back(copied_mesh);
        combined.push_back(copied_model);
        reopened.replace_all(combined);
        require(reopened.members(copied_model.id) == std::vector<AssetId>{copied_mesh.id} &&
                    reopened.members(model_id).size() == 2,
                "Copied source reused original subasset identity");
        auto cycle_mesh = renamed;
        cycle_mesh.source = "Assets/renamed.gltf";
        cycle_mesh.dependencies = {model_id};
        cycle_mesh.dependency_edges = {
            {model_id, "model", AssetDependencyKind::Build, "wrong-parent-dependency", {}}};
        rejects([&] { reopened.replace(cycle_mesh); }, "cycle");
        std::cout << "Subasset families, stable keys, tombstones, relocation and graph ownership "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
