#include "texture_authoring.hpp"
#include "texture_importer.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry>
texture_import_registry(const std::filesystem::path& worker) {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(asset_detail::texture_importer(worker, false));
    registry->add(asset_detail::texture_importer(worker, true));
    registry->seal();
    return registry;
}
ImportTarget desktop_texture_target() {
#ifdef _WIN32
    return {"windows", "d3d12", "desktop"};
#else
    return {"linux", "none", "cpu"};
#endif
}
void prepare_texture_publication(AssetPublicationCandidate& c, const AssetImportPlan& plan) {
    if (plan.input.output_format != "forge.texture-bundle" || !c.sidecar.identity.entries.empty())
        throw std::runtime_error("Texture publication cannot replace another asset family");
    auto& identity = c.sidecar.identity;
    identity.owner = c.ticket.owner;
    identity.source = c.ticket.source;
    identity.source_digest = c.input.source_digest;
    identity.evidence_schema = "forge.texture.single.v1";
    c.records = {{c.ticket.owner, "texture", c.ticket.source, 1, {}}};
}
} // namespace forge
