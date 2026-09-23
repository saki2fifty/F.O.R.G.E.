#include "collision_bundle.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
namespace forge::collision_detail {
namespace {
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
} // namespace
std::vector<ArtifactFile> encode_bundle(AssetId id, const ResolvedCollisionSource& source) {
    require(bool(id), "Collision bundle requires AssetId");
    auto geometry = encode_collision(source.data);
    nlohmann::json removed = nlohmann::json::object();
    for (const auto& [member, count] : source.removed_triangles)
        removed[member.str()] = count;
    const auto text = nlohmann::json{
        {"kind", "forge.collision-bundle"},
        {"version", 1},
        {"asset_id", id},
        {"jolt_revision", jolt_revision},
        {"geometry_sha256", asset_detail::content_digest(geometry)},
        {"removed_triangles",
         removed}}.dump();
    auto bytes = std::as_bytes(std::span(text));
    return {{"collision.json", {bytes.begin(), bytes.end()}},
            {"collision.bin", std::move(geometry)}};
}
Bundle decode_bundle(std::span<const ArtifactFile> files, AssetId expected) {
    require(files.size() == 2, "Collision bundle must contain metadata and geometry");
    const ArtifactFile *meta = nullptr, *geometry = nullptr;
    for (const auto& file : files) {
        if (file.name == "collision.json" && !meta)
            meta = &file;
        else if (file.name == "collision.bin" && !geometry)
            geometry = &file;
        else
            throw std::runtime_error("Unexpected/duplicate collision bundle file");
    }
    require(meta && geometry, "Incomplete collision bundle");
    const auto j = asset_detail::parse_bounded_json(meta->bytes, 256 * 1024, 16384, 8);
    require(j.at("kind") == "forge.collision-bundle" && j.at("version").is_number_integer() &&
                j.at("version") == 1 && j.at("jolt_revision") == jolt_revision,
            "Unsupported collision bundle/cooker provenance");
    Bundle out;
    out.asset = j.at("asset_id").get<AssetId>();
    require(bool(out.asset) && (!expected || expected == out.asset),
            "Collision bundle identity mismatch");
    require(geometry->bytes.size() <= CollisionLimits{}.bytes &&
                j.at("geometry_sha256") == asset_detail::content_digest(geometry->bytes),
            "Collision geometry checksum/size mismatch");
    out.geometry = decode_collision(geometry->bytes);
    const auto& removed = j.at("removed_triangles");
    require(removed.is_object() && removed.size() <= out.geometry.nodes.size(),
            "Invalid collision generation diagnostics");
    for (const auto& [key, value] : removed.items()) {
        const auto id = CollisionMemberId::parse(key);
        require(value.is_number_unsigned() &&
                    value.get<std::uint64_t>() <= CollisionLimits{}.triangles &&
                    std::any_of(out.geometry.nodes.begin(), out.geometry.nodes.end(),
                                [&](const auto& n) {
                                    return n.id == id && n.kind == CollisionKind::TriangleMesh;
                                }),
                "Invalid removed collision triangle count/member");
        out.removed_triangles.emplace(id, value.get<std::size_t>());
    }
    return out;
}
} // namespace forge::collision_detail
