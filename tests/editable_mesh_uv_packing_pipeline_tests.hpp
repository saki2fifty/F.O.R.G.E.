#pragma once
namespace {
void packing_pipeline(const std::filesystem::path& root,
                      const std::shared_ptr<ProjectLease>& lease) {
    const std::filesystem::path source = "Assets/Packing.mesh.json";
    auto document = EditableMeshDocument::create(lease, source);
    const auto original = document->source().document;
    document->edit(document->revision(), "Pack all UV islands", [](Json& draft) {
        EditableMeshSource mesh{draft};
        std::vector<std::uint32_t> faces;
        for (const auto& row : draft.at("faces"))
            faces.push_back(row.at("id").get<std::uint32_t>());
        mesh.pack_uv_islands(faces, .02, true);
        draft = std::move(mesh.document);
    });
    const auto applied = document->source().document;
    document->undo();
    require(document->source().document == original, "Packing pipeline Undo changed topology");
    document->redo();
    require(document->source().document == applied, "Packing pipeline Redo differs");
    document->save();
    EditableMeshDocument reopened(lease, source);
    require(reopened.source().document == applied, "Packing source save/reopen differs");
    const auto expected = reopened.source().cook();
    AssetImportService service(lease, editable_mesh_import_registry(), {"linux", "none", "cpu"});
    const auto receipt = publish(service, source, reopened.source().asset());
    require(receipt.published, "Packed Mesh publication failed: " + receipt.diagnostic);
    auto check = [&](const std::filesystem::path& content,
                     std::shared_ptr<const AssetCatalog> catalog) {
        ResourcePool<MeshAsset> pool;
        const auto ticket = request_model_mesh(pool, content, catalog, {reopened.source().asset()});
        require(pool.wait(ticket, 5s) && bool(pool.acquire(ticket)),
                "Packed Mesh resource unavailable");
        const auto resource = pool.acquire(ticket);
        const auto& actual = resource->mesh;
        require(actual.lods[0].parts[0].indices == expected.lods[0].parts[0].indices,
                "Packed Mesh resource changed indices");
        for (const auto& stream : expected.lods[0].parts[0].streams)
            require(actual.lods[0].parts[0].find(stream.semantic) &&
                        actual.lods[0].parts[0].find(stream.semantic)->values == stream.values,
                    "Packed Mesh resource/cook stream differs");
    };
    check(root, std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    const auto output = root.parent_path() / ("packing-export-" + AssetId::generate().str());
    const std::array roots{reopened.source().asset()};
    package_runtime_content(root, output, roots, {"linux", "none"});
    const auto relocated = root.parent_path() / ("packing-relocated-" + AssetId::generate().str());
    std::filesystem::rename(output, relocated);
    require(!std::filesystem::exists(relocated / source),
            "Packing source leaked into runtime content");
    const auto offline = root / "Assets/Packing.mesh.offline";
    std::filesystem::rename(root / source, offline);
    check(relocated,
          std::make_shared<const AssetCatalog>(open_runtime_content(relocated, {"linux", "none"})));
    std::filesystem::rename(offline, root / source);
    std::cout << "Packing history/source/publication/resource and source-offline relocated export "
                 "agree\n";
}
} // namespace
