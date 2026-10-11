#pragma once
#include "model_importer.hpp"
#include <forge/gltf_source.hpp>
namespace forge::test {
// Reuse the standard recipe under sanitizers: bounded ASan workers cannot reserve shadow memory.
class DirectModelImporter final : public AssetImporter {
    std::shared_ptr<const AssetImporter> delegate_;
    std::filesystem::path converter_;

  public:
    DirectModelImporter(std::shared_ptr<const AssetImporter> d, std::filesystem::path converter)
        : AssetImporter(d->descriptor(), d->settings()), delegate_(std::move(d)),
          converter_(std::move(converter)) {}
    ImportProbeResult probe(const ImportProbe& p) const override { return delegate_->probe(p); }
    AssetImportPlan discover(const AssetImportRequest& r, std::stop_token s) const override {
        return delegate_->discover(r, s);
    }
    void validate(const CachedArtifact& a) const override { delegate_->validate(a); }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& r, const AssetImportPlan& p, std::stop_token s,
                    const std::function<void(double, std::string)>&) const override {
        const auto current = discover(r, s);
        if (current.input.document() != p.input.document())
            throw std::runtime_error("Direct fixture input stale");
        auto files = asset_detail::execute_model_recipe(
            {{{"recipe", descriptor().id},
              {"revision", asset_detail::model_recipe_revision()},
              {"settings", r.settings},
              {"source_digest", p.input.source_digest},
              {"backend", r.target.backend}},
             asset_detail::encode_gltf_snapshot(
                 capture_gltf_source(r.project, r.source, asset_detail::model_cook_extensions(), {},
                                     s),
                 {}, s)},
            s);
        return asset_detail::finish_model_recipe(
            std::move(files), converter_, r.project, p.input.source_digest,
            p.data.at("converter_sha256").get<std::string>(), s);
    }
};
} // namespace forge::test
