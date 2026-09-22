#pragma once
#include <forge/derived_cache.hpp>
#include <forge/subasset_identity.hpp>
#include <optional>
namespace forge::asset_detail {
struct ModelMemberFile {
    std::string file, digest;
    std::uint64_t bytes = 0;
};
struct ModelImportMember {
    SubassetObservation identity;
    ModelMemberFile artifact;
    // Inline immutable hierarchy selector, mutually exclusive with an artifact.
    // The owning model.json authenticates the bytes; no per-node marker files.
    std::optional<std::uint32_t> node;
    // Named cooked slot -> candidate-local member address. Owner-thread
    // reconciliation binds these to durable AssetIds in catalog dependency edges.
    std::map<std::string, std::string> bindings;
    std::optional<std::uint32_t> material_variant;
};
struct ModelBundleIndex {
    // Version1 lacks explicit TRS; version2 lacks durable node-member bindings.
    // Version4 adds stable material-variant members. Versions1–3 remain readable.
    unsigned version = 4;
    std::string source_digest;
    std::vector<ModelImportMember> members;
    // Immutable asset hierarchy, never a second mutable gameplay hierarchy.
    nlohmann::json hierarchy = nlohmann::json::object();
    std::vector<std::string> diagnostics;
};
std::vector<std::byte> encode_model_bundle_index(const ModelBundleIndex& index);
ModelBundleIndex decode_model_bundle_index(std::span<const std::byte> bytes);
// CPU cooked validation only: no source codecs, world mutation or device creation.
enum class ModelValidation { Complete, GeometryStage };
ModelBundleIndex validate_model_bundle(std::span<const ArtifactFile> files,
                                       ModelValidation stage = ModelValidation::Complete);
// Joins already cooked geometry with admitted official converter outputs. No disk
// publication or durable identity assignment occurs until the entire family passes.
std::vector<ArtifactFile> complete_model_animation(std::vector<ArtifactFile> geometry,
                                                   const nlohmann::json& metadata,
                                                   std::vector<ArtifactFile> archives,
                                                   const nlohmann::json& provenance);
} // namespace forge::asset_detail
