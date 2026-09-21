#pragma once
#include "content_thumbnails.hpp"
#include <SDL3/SDL.h>
inline void check_thumbnail_cache(forge::DiligentPresentation& presentation,
                                  Diligent::IDeviceContext* context,
                                  const std::filesystem::path& project,
                                  std::shared_ptr<forge::MeshResourceHost> host) {
    using namespace forge;
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    ContentThumbnails preview(presentation, context, project, host);
    const auto catalog = host->catalog();
    auto record =
        std::find_if(catalog->records().begin(), catalog->records().end(), [](const auto& entry) {
            return entry.second.type == "texture" && !entry.second.subasset;
        });
    require(record != catalog->records().end(), "Thumbnail fixture needs its imported texture");
    const auto texture = record->first;
    auto pump = [&](std::shared_ptr<const AssetCatalog> selected, bool failing) {
        const auto deadline = SDL_GetTicks() + 6000;
        for (;;) {
            preview.begin(selected);
            auto view = preview.request(texture);
            preview.advance();
            preview.after_submission();
            if ((!failing && view.image && view.status == "Published asset thumbnail") ||
                (failing && view.status.starts_with("Thumbnail: ")))
                return view;
            if (SDL_GetTicks() >= deadline)
                throw std::runtime_error("Thumbnail fixture timed out: " + view.status);
            SDL_Delay(1);
        }
    };
    const auto initial = pump(catalog, false);
    const auto completed = preview.completed();
    require(completed == 1 && initial.image, "Real Texture thumbnail was not rendered");
    require(preview.ready_count() == 1,
            "Ready thumbnail count must describe unique current assets");
    auto unrelated = std::make_shared<AssetCatalog>(*catalog);
    unrelated->add({AssetId::generate(), "texture", "Assets/unrelated.png"});
    const auto unchanged = pump(unrelated, false);
    require(unchanged.image == initial.image && preview.completed() == completed,
            "Unrelated catalog edit rendered or replaced a cached thumbnail");
    auto broken = std::make_shared<AssetCatalog>(*unrelated);
    auto bad = record->second;
    bad.metadata["forge.import"]["key"] = std::string(64, 'f');
    bad.metadata["forge.import"]["generation"] =
        bad.metadata["forge.import"].at("generation").get<std::uint64_t>() + 1;
    broken->replace(bad);
    const auto failed = pump(broken, true);
    require(failed.image == initial.image && preview.completed() == completed,
            "Failed thumbnail replacement discarded or overwrote its previous good image");
    require(preview.ready_count() == 0,
            "Retained old thumbnail was incorrectly counted as a ready current revision");
    // Metadata admission is bounded even before any decoded/GPU candidate exists.
    auto many = std::make_shared<AssetCatalog>(*catalog);
    std::vector<AssetId> ids;
    for (unsigned i = 0; i < 129; ++i) {
        auto member = record->second;
        member.id = AssetId::generate();
        member.source = "Assets/thumb-" + std::to_string(i) + ".png";
        ids.push_back(member.id);
        many->add(std::move(member));
    }
    preview.begin(many);
    for (const auto id : ids)
        (void)preview.request(id);
    require(preview.size() == 128, "Thumbnail LRU exceeded its entry cap");
    require(preview.request(ids.back()).status.find("cache is full") != std::string::npos,
            "Current-frame thumbnail admission silently evicted a pinned image");
    preview.after_submission();
    preview.begin(many);
    require(preview.request(ids.back()).status.find("cache is full") == std::string::npos &&
                preview.size() == 128,
            "Offscreen thumbnail entries did not become eligible for eviction");
}
