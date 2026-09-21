#pragma once
#include "asset_reimport.hpp"
inline void test_model_family_watch(std::shared_ptr<const forge::ProjectLease> lease,
                                    std::shared_ptr<const forge::AssetImporterRegistry> registry,
                                    forge::AssetId owner) {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    AssetReimportService watch(lease, {{"forge.model.gltf",
                                        {"portable", "none", "cpu"},
                                        registry,
                                        [](auto& c, const auto& p, const auto& catalog) {
                                            prepare_model_publication(c, p, catalog);
                                        }}});
    auto changed = AssetCatalog::open_project(lease->root());
    auto records = std::vector<AssetRecord>{};
    AssetId member;
    for (auto [id, record] : changed.records()) {
        if (id == owner || (record.subasset && record.subasset->owner == owner)) {
            record.metadata["forge.import"]["generation"] = std::uint64_t(2);
            if (record.type == "material")
                member = id;
        }
        records.push_back(std::move(record));
    }
    check(bool(member), "Watch regression needs a model material member");
    auto consumer = changed.records().at(owner);
    consumer.id = AssetId::generate();
    consumer.source = "Assets/consumer.gltf";
    consumer.dependency_edges = {
        {member, "material", AssetDependencyKind::Build, "fixture.external", ""}};
    consumer.dependencies = {member};
    consumer.source_dependencies.clear();
    records.push_back(consumer);
    changed.replace_all(std::move(records));
    watch.catalog_changed(std::make_shared<const AssetCatalog>(std::move(changed)));
    const auto activity = watch.activity();
    check(!activity.contains(owner), "Published model family scheduled its own endless reimport");
    check(activity.contains(consumer.id) && watch.queued() == 1,
          "Suppressing family self-invalidation lost an external Build dependent");
}
