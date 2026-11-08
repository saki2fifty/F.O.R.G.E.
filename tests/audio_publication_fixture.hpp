#pragma once
#include "asset_bytes.hpp"
#include "audio_authoring.hpp"
#include "audio_importer.hpp"
#include <forge/assets.hpp>

namespace forge::test {
inline AssetRecord publish_audio_fixture(const std::filesystem::path& root, std::string name,
                                         bool stream) {
    using namespace asset_detail;
    auto original = AssetCatalog::register_audio_clip(root, "Assets/" + name + ".wav");
    auto importer = audio_importer({});
    AssetImportRequest request{
        original.id, root, original.source, {"windows", "d3d12", "desktop"}, {"forge.audio.wav"}};
    if (stream)
        request.settings = audio_settings().edit(request.settings, "playback_storage", "stream");
    const auto plan = importer->discover(request, {});
    auto files = execute_audio_recipe(
        {{{"recipe", "forge.audio.wav"},
          {"revision", audio_recipe_revision()},
          {"source_digest", plan.input.source_digest},
          {"settings", request.settings}},
         {{"source.wav", read_bytes(root / request.source, max_audio_source_bytes)}}});
    ProjectLease lease(root);
    AssetPublisher publisher(lease);
    AssetPublicationCandidate candidate;
    candidate.ticket = publisher.capture(request.asset, request.source);
    candidate.input = plan.input;
    candidate.sidecar.settings = request.settings;
    candidate.sidecar.build_inputs = plan.input.document();
    candidate.files = std::move(files);
    prepare_audio_publication(candidate, plan);
    return publisher.publish(std::move(candidate), *importer, [](const auto&, const auto&) {})
        .catalog.records()
        .at(original.id);
}
} // namespace forge::test
