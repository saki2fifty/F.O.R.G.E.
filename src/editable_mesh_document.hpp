#pragma once
#include "asset_source_document.hpp"
#include <algorithm>
#include <forge/editable_mesh.hpp>
#include <set>
namespace forge {
struct EditableMeshDocumentTraits {
    using Source = EditableMeshSource;
    static constexpr auto byte_limit = editable_mesh_source_byte_limit;
    static constexpr auto asset_type = MeshAsset::type;
    static constexpr auto suffix = ".mesh.json";
    static Source create(AssetId id) { return Source::create_cube(id); }
    static std::set<std::uint32_t> element_ids(const Source& source, const char* kind) {
        std::set<std::uint32_t> result;
        for (const auto& element : source.document.at(kind))
            result.insert(element.at("id").get<std::uint32_t>());
        return result;
    }
    static void validate_edit(const Source& before, const Source& after) {
        const auto floor = before.document.at("next_id").get<std::uint32_t>();
        if (after.document.at("next_id").get<std::uint32_t>() < floor)
            throw std::runtime_error("Mesh element allocator cannot decrease");
        for (const auto kind : {"vertices", "faces"}) {
            const auto prior = element_ids(before, kind);
            for (const auto identity : element_ids(after, kind))
                if (!prior.contains(identity) && identity < floor)
                    throw std::runtime_error("New Mesh elements cannot reuse reserved identities");
        }
    }
    static void restore_identity_metadata(Source& restored, const Source& current) {
        restored.document["next_id"] =
            std::max(restored.document.at("next_id").get<std::uint32_t>(),
                     current.document.at("next_id").get<std::uint32_t>());
        restored.validate();
    }
};
using EditableMeshDocument = AssetSourceDocument<EditableMeshDocumentTraits>;
} // namespace forge
