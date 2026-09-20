#include "shader_authoring.hpp"
#include <forge/shader_asset.hpp>
namespace forge {
void prepare_shader_publication(AssetPublicationCandidate& candidate, const AssetImportPlan& plan) {
    if (plan.input.importer != "forge.shader.diligent" ||
        plan.data.at("asset_id").get<AssetId>() != candidate.ticket.owner ||
        !candidate.sidecar.identity.entries.empty() || candidate.files.size() != 1 ||
        candidate.files.front().name != "program.shader")
        throw std::runtime_error("Shader publication identity/file set mismatch");
    const auto shader = decode_shader(candidate.files.front().bytes);
    if (shader.build_key != plan.data.at("compiler_input_key").get<std::string>())
        throw std::runtime_error("Shader candidate disagrees with captured compiler inputs");
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.shader.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          "shader",
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.shader",
                            {{"version", 1},
                             {"layout", shader.layout_digest()},
                             {"compiler_input_key", shader.build_key}}}}}};
}
} // namespace forge
