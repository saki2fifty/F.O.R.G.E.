#include "asset_storage.hpp"
#include <forge/ui_assets.hpp>
#include <iostream>

namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid UI draft accepted");
}
std::string text(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace
int main(int argc, char** argv) {
    try {
        using namespace forge;
        check(argc == 2, "Expected scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{root};
        const std::string saved = "<rml><body><p>Saved</p></body></rml>";
        const std::string draft = "<rml><head><link type=\"text/rcss\" href=\"hud.rcss\" /></head>"
                                  "<body><div id=\"panel\">Unsaved</div></body></rml>";
        asset_storage::replace(root / "Assets/hud.rml", saved);
        asset_storage::replace(root / "Assets/other.rml", saved);
        asset_storage::replace(root / "Assets/hud.rcss", "body {color:red;}");
        const auto record = register_ui_document(root, "Assets/hud.rml");
        const auto other = register_ui_document(root, "Assets/other.rml");
        const auto index = asset_storage::read(AssetCatalog::project_index(root));
        UiResources candidate(root);
        check(candidate.draft_document({record.id}, "Assets/hud.rml", draft) == "Assets/hud.rml",
              "Draft locator changed");
        const auto& admitted = candidate.read("Assets/hud.rml");
        check(text(admitted) == draft && candidate.bytes() == draft.size(),
              "Candidate did not retain draft bytes");
        rejects([&] { candidate.draft_document({record.id}, "Assets/hud.rml", saved); });
        rejects([&] { candidate.draft_document({other.id}, "Assets/other.rml", saved); });
        candidate.read(candidate.join("Assets/hud.rml", "hud.rcss"));
        candidate.automatic("Assets/hud.rml");
        check(text(admitted) == draft && candidate.source_count() == 2 &&
                  candidate.snapshot().documents.at(record.id) == "Assets/hud.rml",
              "Draft references or resource accounting changed");
        UiResources disk(root);
        disk.document({record.id});
        check(text(disk.read("Assets/hud.rml")) == saved &&
                  disk.snapshot().sources.front().digest !=
                      candidate.snapshot().sources.front().digest,
              "Draft escaped its candidate");
        rejects([&] { UiResources(root).draft_document({record.id}, "Assets/other.rml", draft); });
        rejects([&] {
            UiResources(root).draft_document({AssetId::generate()}, "Assets/hud.rml", draft);
        });
        rejects([&] { UiResources(root).draft_document({record.id}, "../hud.rml", draft); });
        rejects([&] { UiResources(root).draft_document({record.id}, "Assets/hud.rml", "<rml>"); });
        rejects([&] {
            UiResources(root).draft_document({record.id}, "Assets/hud.rml",
                                             std::string(256 * 1024 + 1, 'x'));
        });
        rejects([&] {
            UiResources(root).draft_document({record.id}, "Assets/hud.rml",
                                             std::string("<rml>\0</rml>", 12));
        });
        UiResources used(root);
        used.read("Assets/hud.rcss");
        rejects([&] { used.draft_document({record.id}, "Assets/hud.rml", draft); });
        UiResources retry(root);
        rejects([&] { retry.draft_document({record.id}, "Assets/hud.rml", "invalid"); });
        check(retry.bytes() == 0 && retry.source_count() == 0,
              "Rejected draft consumed candidate budget");
        retry.draft_document({record.id}, "Assets/hud.rml", draft);
        check(asset_storage::read(root / "Assets/hud.rml") == saved &&
                  asset_storage::read(AssetCatalog::project_index(root)) == index,
              "Preview wrote saved content or catalog");
        // A valid correction must not depend on admitting the saved invalid bytes.
        asset_storage::replace(root / "Assets/hud.rml", "<rml>");
        UiResources corrected(root);
        corrected.draft_document({record.id}, "Assets/hud.rml", draft);
        check(text(corrected.read("Assets/hud.rml")) == draft,
              "Corrected draft loaded invalid saved source");
        rejects([&] { UiResources(root).document({record.id}); });
        asset_storage::replace(root / "forge.runtime-content.json",
                               R"({"format":"forge.runtime-content","version":1,"files":{}})");
        rejects([&] { UiResources(root).draft_document({record.id}, "Assets/hud.rml", draft); });
        std::cout << "UI draft resource admission passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
