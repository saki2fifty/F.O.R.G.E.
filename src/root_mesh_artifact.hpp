#pragma once
#include "asset_bytes.hpp"
#include <cmath>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/mesh_asset.hpp>
namespace forge::asset_detail {
inline bool terrain_mesh_publication(const AssetRecord& root) {
    return root.metadata.at("forge.import").at("importer") == "forge.terrain.heightfield";
}
inline void validate_root_mesh_publication(const AssetRecord& root) {
    const auto& selected = root.metadata.at("forge.import");
    const auto& mesh = root.metadata.at("forge.mesh");
    const bool terrain = terrain_mesh_publication(root);
    if (root.type != MeshAsset::type || root.subasset ||
        (terrain ? selected.at("importer_revision") != "forge.terrain.heightfield.v1"
                 : selected.at("importer") != "forge.mesh.editable" ||
                       selected.at("importer_revision") != "forge.mesh.editable.v1") ||
        selected.at("output_format") != "forge.mesh-bundle" || selected.at("output_version") != 1 ||
        mesh.at("version") != 1 || mesh.at("file") != "mesh.bin" ||
        !selected.at("generation").is_number_unsigned() ||
        selected.at("generation").get<std::uint64_t>() == 0)
        throw std::runtime_error("Authored root Mesh publication recipe/version is incompatible");
    if (terrain) {
        const auto& source = root.metadata.at("forge.terrain");
        if (source.at("version") != 1 || source.at("profile") != "heightfield_mesh1")
            throw std::runtime_error("Terrain Mesh publication profile is incompatible");
    }
}
inline void validate_heightfield_mesh(const MeshData& mesh) {
    validate_mesh(mesh, {32 * 1024 * 1024, 8 * 129 * 129, 8 * 128 * 128 * 6, 8, 4, 0, 1});
    if (mesh.material_slots != 1 || mesh.lods.size() != 1 || mesh.lods[0].parts.empty())
        throw std::runtime_error("Terrain Mesh has an incompatible material/LOD layout");
    for (const auto& part : mesh.lods[0].parts) {
        const auto side = unsigned(std::sqrt(part.vertices));
        if (side < 2 || side > 129 || side * side != part.vertices ||
            part.topology != MeshTopology::Triangles || part.material_slot != 0 ||
            part.streams.size() != 4 ||
            part.indices.size() != std::size_t(side - 1) * (side - 1) * 6 ||
            !part.find("POSITION") || !part.find("NORMAL") || !part.find("TANGENT") ||
            !part.find("TEXCOORD_0"))
            throw std::runtime_error("Terrain Mesh differs from the heightfield profile");
        if (part.find("POSITION")->components != 3 || part.find("NORMAL")->components != 3 ||
            part.find("TANGENT")->components != 4 || part.find("TEXCOORD_0")->components != 2)
            throw std::runtime_error("Terrain Mesh stream dimensions differ from the profile");
        std::size_t i = 0;
        for (unsigned z = 0; z + 1 < side; ++z)
            for (unsigned x = 0; x + 1 < side; ++x) {
                const auto a = z * side + x, b = a + 1, c = a + side, d = c + 1;
                for (const auto index : {a, c, b, b, c, d})
                    if (part.indices[i++] != index)
                        throw std::runtime_error("Terrain Mesh grid winding/layout changed");
            }
    }
}
inline MeshData admit_root_mesh(const CachedArtifact& artifact, const AssetRecord& root,
                                MeshLimits limits = {}) {
    validate_root_mesh_publication(root);
    const auto& selected = root.metadata.at("forge.import");
    const auto& mesh = root.metadata.at("forge.mesh");
    const auto& input = artifact.manifest.at("inputs");
    for (const auto* name : {"importer", "importer_revision", "output_format", "output_version",
                             "platform", "backend", "profile"})
        if (input.at(name) != selected.at(name))
            throw std::runtime_error("Root Mesh immutable artifact recipe differs from catalog");
    if (artifact.files.size() != 1 || artifact.files[0].name != "mesh.bin" ||
        content_digest(artifact.files[0].bytes) != mesh.at("sha256").get<std::string>() ||
        mesh.at("bytes") != artifact.files[0].bytes.size() ||
        asset_build_digest(artifact.manifest.at("files")) !=
            selected.at("artifact_digest").get<std::string>() ||
        input.at("source") != selected.at("source_digest"))
        throw std::runtime_error("Root Mesh artifact/catalog provenance differs");
    auto decoded = decode_mesh(artifact.files[0].bytes, limits);
    if (terrain_mesh_publication(root))
        validate_heightfield_mesh(decoded);
    return decoded;
}
} // namespace forge::asset_detail
