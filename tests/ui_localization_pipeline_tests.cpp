#include "asset_file_operations.hpp"
#include "asset_reference_impact.hpp"
#include "runtime_package.hpp"
#include "ui_localization_admission.hpp"
#include "ui_localization_authoring.hpp"
#include <forge/asset_discovery.hpp>
#include <forge/derived_cache.hpp>
#include <forge/ui_assets.hpp>
#include <iostream>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid source operation was accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need localization pipeline scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        atomic_write(root / "forge.assets.json",
                     Json{{"version", 1}, {"assets", Json::array()}}.dump());
        const auto lease = std::make_shared<ProjectLease>(root);
        auto doc = UiLocalizationDocument::create(lease, "Assets/Menu.localization.json");
        const auto asset = doc->source().asset(), other = AssetId::generate();
        const auto initial = doc->source().document;
        const auto revision = doc->revision();
        doc->edit(revision, "Add literal message", [](Json& d) {
            d["locales"]["en"]["messages"]["status"] = "<b>Café</b> {{literal}}";
            d["vendor"] = {{"opaque", "Retain this value"}};
        });
        const auto changed = doc->source().document;
        rejects([&] { doc->edit(revision, "Stale", [](Json&) {}); });
        rejects([&] {
            doc->edit(doc->revision(), "Identity change", [&](Json& d) { d["asset_id"] = other; });
        });
        rejects([&] {
            doc->edit(doc->revision(), "Invalid locale",
                      [](Json& d) { d["default_locale"] = "x"; });
        });
        check(doc->source().document == changed, "Rejected edit changed source or history");
        doc->undo();
        check(doc->source().document == initial, "Undo changed locale identity or initial data");
        doc->redo();
        check(doc->source().document == changed, "Redo lost literal or extension data");
        doc->save();
        AssetImportService imports(lease, ui_localization_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        const auto publish = [&] {
            imports.submit(
                imports.prepare(doc->locator(), {}, asset),
                [](auto& candidate, const auto& plan, const auto&) {
                    prepare_ui_localization_publication(candidate, plan);
                },
                [](const auto&, const auto&) {});
            check(imports.wait_idle(10s), "Localization publication timed out");
            const auto outcomes = imports.poll();
            check(outcomes.size() == 1, "Missing localization publication receipt");
            return outcomes.front();
        };
        auto outcome = publish();
        check(outcome.published, outcome.diagnostic);
        auto catalog = AssetCatalog::open_project(root);
        const auto first = catalog.records().at(asset);
        check(first.type == UiLocalizationAsset::type && first.schema_version == 1 &&
                  first.metadata.at("forge.import").at("importer") == "forge.ui.localization" &&
                  first.metadata.at("forge.import").at("output_format") == "forge.ui-localization",
              "Publication changed the selected type/recipe");
        const auto key = first.metadata.at("forge.import").at("key").get<std::string>();
        const auto registry = ui_localization_import_registry();
        const auto importer = registry->find("forge.ui.localization");
        check(bool(importer), "Localization importer missing");
        DerivedDataCache cache(root);
        const auto artifact =
            cache.load_selected(key, [&](const auto& a) { importer->validate(a); });
        check(artifact.files.size() == 1 && artifact.files.front().name == "localization.json",
              "Publication did not select one canonical cooked table");
        const auto admitted = UiLocalizationSource::parse(artifact.files.front().bytes);
        check(admitted.asset() == asset && admitted.document == changed &&
                  admitted.compiled().message("status", "fr").text == "<b>Café</b> {{literal}}",
              "Cooked table lost identity, literal messages, fallback or opaque data");
        const auto runtime_data = admit_ui_localization_artifact(first, artifact);
        check(runtime_data.asset == asset, "Typed runtime admission changed catalog identity");
        auto literal_source = changed;
        literal_source["locales"]["en"]["messages"]["asset_like"] = asset.str();
        const auto copied_id = AssetId::generate();
        const auto copied = Json::parse(rewrite_authored_asset(
            first, literal_source.dump(), "Assets/Copy.localization.json", {{asset, copied_id}}));
        check(copied.at("asset_id").get<AssetId>() == copied_id &&
                  copied.at("locales") == literal_source.at("locales") &&
                  copied.at("vendor") == literal_source.at("vendor"),
              "Catalog duplication rewrote literal UUID-like text or opaque source data");
        rejects([&] {
            rewrite_authored_asset(first, literal_source.dump(), "Assets/Wrong.json",
                                   {{asset, copied_id}});
        });
        literal_source.erase("vendor");
        const std::array documents{AssetReferenceDocument{doc->locator(), literal_source}};
        const auto references =
            collect_asset_references({{"components", Json::array()}}, documents);
        check(references.references.empty() && references.uninspected.empty(),
              "Literal message text was treated as a reference or opaque scope");
        const auto discovered = scan_asset_sources(root);
        check(discovered.complete &&
                  discovered.files.at(doc->locator()).source_kind == UiLocalizationAsset::type,
              "Content discovery did not recognize localization sources");
        auto malformed = first;
        malformed.metadata["forge.import"]["settings_digest"] = std::string(64, '0');
        rejects([&] { (void)admit_ui_localization_artifact(malformed, artifact); });
        malformed = first;
        malformed.metadata["forge.ui_localization"]["bytes"] = 1;
        rejects([&] { (void)admit_ui_localization_artifact(malformed, artifact); });
        auto wrong_identity = artifact;
        wrong_identity.key = std::string(64, '0');
        rejects([&] { (void)admit_ui_localization_artifact(first, wrong_identity); });
        const auto hidden = root / "Source-offline";
        std::filesystem::rename(root / doc->locator(), hidden);
        UiResources resources(root);
        const auto& retained = resources.localization({asset});
        check(retained.message("status", "fr").text == "<b>Café</b> {{literal}}" &&
                  &resources.localization({asset}) == &retained && resources.source_count() == 0 &&
                  resources.resource_count() == 1 && resources.bytes() > 0 &&
                  resources.snapshot().sources.empty(),
              "UI resource admission used editable source, duplicated a table or exposed it as raw "
              "UI source");
        std::filesystem::rename(hidden, root / doc->locator());
        UiLocalizationDocument reopened(lease, doc->locator());
        check(reopened.source().document == changed, "Reopen changed saved localization values");
        auto wrong_file = artifact;
        wrong_file.files.front().name = "wrong.json";
        rejects([&] { importer->validate(wrong_file); });
        const auto saved = changed.dump(2) + "\n";
        atomic_write(root / doc->locator(), "{\"invalid\":true}\n");
        outcome = publish();
        check(!outcome.published && !outcome.diagnostic.empty(),
              "Malformed source did not produce a failed publication receipt");
        check(AssetCatalog::open_project(root).document() == catalog.document() &&
                  cache.load_selected(key, [&](const auto& a) { importer->validate(a); })
                          .files.front()
                          .bytes == artifact.files.front().bytes,
              "Failed source admission replaced the last good selection");
        atomic_write(root / doc->locator(), saved);
        doc = std::make_unique<UiLocalizationDocument>(lease, doc->locator());
        doc->edit(doc->revision(), "Translate status",
                  [](Json& d) { d["locales"]["fr"]["messages"]["status"] = "État prêt"; });
        doc->save();
        outcome = publish();
        check(outcome.published, outcome.diagnostic);
        catalog = AssetCatalog::open_project(root);
        const auto second = catalog.records().at(asset);
        check(second.id == first.id &&
                  second.metadata.at("forge.import").at("generation") >
                      first.metadata.at("forge.import").at("generation") &&
                  second.metadata.at("forge.import").at("key") != key &&
                  admitted.compiled().message("status", "fr").fallback,
              "Repair changed logical identity or mutated the retained predecessor");
        UiResources replacement(root);
        check(resources.localization({asset}).message("status", "fr").fallback &&
                  replacement.localization({asset}).message("status", "fr").text == "État prêt",
              "New UI candidate mutated a retained predecessor or loaded an old selection");
        const auto package = root.parent_path() / (asset.str() + "-export");
        const std::array roots{asset};
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = root.parent_path() / (asset.str() + " Relocated game");
        std::filesystem::rename(package, moved);
        check(!std::filesystem::exists(moved / doc->locator()),
              "Localization package retained editable source");
        UiResources exported(moved);
        check(exported.localization({asset}).message("status", "fr").text == "État prêt",
              "Relocated source-free package lost its selected localization table");
        std::filesystem::remove_all(moved);
        std::filesystem::remove_all(root);
        std::cout << "Localization source history, publication, cooked identity and last-good "
                     "repair passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
