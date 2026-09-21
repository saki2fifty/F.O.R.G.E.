#pragma once
#include "asset_bytes.hpp"
#include "asset_reimport.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include <forge/material_source.hpp>
#include <fstream>
inline void test_material_watch(const std::filesystem::path& root) {
    using namespace forge;
    using namespace forge::asset_detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const std::string& message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    std::filesystem::create_directories(root / "Assets");
    const auto write = [&](const char* path, const MaterialSource& source) {
        std::ofstream file(root / path);
        file << source.document.dump();
        file.close();
        check(bool(file), "Watch fixture source write failed");
    };
    auto lease = std::make_shared<ProjectLease>(root);
    auto source = MaterialSource::create(AssetId::generate());
    auto instance = MaterialSource::create(AssetId::generate());
    instance.document["base"] = source.asset();
    write("Assets/base.material.json", source);
    write("Assets/instance.material.json", instance);
    auto registry = material_import_registry();
    AssetImportService import(lease, registry, {"linux", "none", "cpu"});
    for (const auto& [path, id] : {std::pair{"Assets/base.material.json", source.asset()},
                                   std::pair{"Assets/instance.material.json", instance.asset()}}) {
        import.submit(
            import.prepare(path, {}, id),
            [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
            [](const auto&, const auto&) {});
        check(import.wait_idle(10s), "Watch fixture initial import stalled");
        const auto result = import.poll();
        check(result.size() == 1 && result[0].published, "Watch fixture initial import failed");
        check(result[0].publication->written_sources.size() == 2,
              "Publication omitted exact catalog/sidecar write receipt");
        for (const auto& [locator, digest] : result[0].publication->written_sources)
            check(content_digest(read_bytes(root / locator, 64 * 1024 * 1024)) == digest,
                  "Publication write receipt differs from committed bytes");
    }
    SourceScanOptions options;
    options.include_project_root = true;
    AssetReimportService automatic(
        lease,
        {{"forge.material.builtin",
          {"linux", "none", "cpu"},
          registry,
          [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); }}},
        options, 10min, 0ms);
    std::vector<AssetImportOutcome> receipts;
    const auto poll = [&] {
        for (auto& receipt : automatic.poll())
            receipts.push_back(std::move(receipt));
    };
    const auto finish = [&] {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        do {
            poll();
            check(std::chrono::steady_clock::now() < deadline,
                  "Automatic material reimport stalled");
            std::this_thread::sleep_for(1ms);
        } while (!automatic.complete() || automatic.scanning() || automatic.queued() ||
                 !automatic.jobs().empty());
    };
    const auto factor = [&] {
        const auto catalog = AssetCatalog::open_project(root);
        const auto selected = load_material_selection(root, catalog, {instance.asset()});
        return selected.data.values.parameters.at("roughnessFactor").value[0];
    };
    finish();
    check(receipts.empty(), "Unchanged committed assets were rebuilt at watch startup");
    receipts.clear();
    automatic.rescan();
    finish();
    check(receipts.empty(), "Publisher self-writes caused an automatic reimport loop");
    {
        const auto sidecar = root / AssetPublisher::sidecar_path("Assets/base.material.json");
        auto document = nlohmann::json::parse(std::ifstream(sidecar));
        document["watch_fixture_unknown"] = "preserve me";
        std::ofstream(sidecar) << document.dump(2);
        automatic.rescan();
        finish();
        check(!receipts.empty() && receipts.front().published,
              "External sidecar-only edit did not trigger publication");
        check(nlohmann::json::parse(std::ifstream(sidecar)).at("watch_fixture_unknown") ==
                  "preserve me",
              "Automatic sidecar update lost unknown metadata");
        receipts.clear();
        automatic.rescan();
        finish();
        check(receipts.empty(), "Committed sidecar digest did not stabilize after publication");
    }
    const auto change = [&](float value) {
        source.document["overrides"]["parameters"]["roughnessFactor"] = {{"type", 0},
                                                                         {"value", {value}}};
        write("Assets/base.material.json", source);
        automatic.rescan();
    };
    change(.65f);
    finish();
    check(factor() == .65f, "Source watch did not rebuild the dependent material instance");
    check(std::count_if(receipts.begin(), receipts.end(),
                        [](const auto& r) { return r.published; }) >= 2,
          "Automatic material chain did not publish both required assets");
    const auto good = AssetCatalog::open_project(root).document();
    receipts.clear();
    change(-1);
    finish();
    check(AssetCatalog::open_project(root).document() == good && !receipts.empty() &&
              !receipts.back().published && !receipts.back().diagnostic.empty(),
          "Failed automatic import changed good publication or lost its diagnostic");
    bool draft_open = true;
    automatic.blocked = [&](AssetId id) { return draft_open && id == source.asset(); };
    receipts.clear();
    change(.33f);
    const auto until = std::chrono::steady_clock::now() + 5s;
    do {
        poll();
        check(std::chrono::steady_clock::now() < until, "Dirty source guard scan stalled");
        std::this_thread::sleep_for(1ms);
    } while (automatic.scanning() || automatic.queued() == 0);
    check(receipts.empty() && automatic.jobs().empty() && factor() == .65f,
          "Automatic reimport bypassed the dirty document guard");
    draft_open = false;
    finish();
    check(factor() == .33f, "Deferred source did not import after document guard released");
    automatic.suspend(true);
    check(automatic.quiescent(), "Idle watcher did not yield project write ownership");
    change(.41f);
    for (int i = 0; i < 5; ++i)
        poll();
    check(automatic.jobs().empty() && factor() == .33f,
          "Suspended watcher published another writer's sources");
    automatic.suspend(false);
    finish();
    check(factor() == .41f, "Resumed watcher missed source changes");
    change(.52f);
    const auto active_deadline = std::chrono::steady_clock::now() + 5s;
    do {
        poll();
        check(std::chrono::steady_clock::now() < active_deadline, "Expected automatic candidate");
        std::this_thread::sleep_for(1ms);
    } while (automatic.jobs().empty());
    automatic.suspend(true);
    const auto drain_deadline = std::chrono::steady_clock::now() + 5s;
    while (!automatic.quiescent()) {
        poll();
        check(std::chrono::steady_clock::now() < drain_deadline,
              "Active import did not drain for source operation");
        std::this_thread::sleep_for(1ms);
    }
    check(factor() == .41f, "Suspending active candidate still published it");
    automatic.suspend(false);
    // New bytes arrive after submission but before the owner drains publication.
    change(.88f);
    finish();
    check(factor() == .88f, "Late automatic result replaced a newer source observation");
    const auto final = AssetCatalog::open_project(root).document();
    std::filesystem::remove(root / "Assets/base.material.json");
    automatic.rescan();
    finish();
    check(AssetCatalog::open_project(root).document() == final && factor() == .88f,
          "Source deletion erased identities or the previous cooked material");

    // A base may have been published by a command-line session while the editor
    // was closed. Startup must validate captured dependency keys too.
    source.document["overrides"]["parameters"]["roughnessFactor"]["value"] = {.91f};
    write("Assets/base.material.json", source);
    import.submit(
        import.prepare("Assets/base.material.json", {}, source.asset()),
        [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
        [](const auto&, const auto&) {});
    check(import.wait_idle(10s), "Offline base publication stalled");
    const auto offline = import.poll();
    check(offline.size() == 1 && offline[0].published, "Offline base publication failed");
    AssetReimportService restarted(
        lease,
        {{"forge.material.builtin",
          {"linux", "none", "cpu"},
          registry,
          [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); }}},
        options, 10min, 0ms);
    const auto restart_deadline = std::chrono::steady_clock::now() + 10s;
    do {
        (void)restarted.poll();
        check(std::chrono::steady_clock::now() < restart_deadline,
              "Startup dependency reconciliation stalled");
        std::this_thread::sleep_for(1ms);
    } while (!restarted.complete() || restarted.scanning() || restarted.queued() ||
             !restarted.jobs().empty());
    check(factor() == .91f, "Startup ignored a stale cooked dependency revision");
    const auto before_delete = AssetCatalog::open_project(root);
    auto after_delete = AssetCatalog(root);
    std::vector<AssetRecord> retained;
    for (const auto& [id, record] : before_delete.records())
        if (id != source.asset())
            retained.push_back(record);
    after_delete.replace_all(retained);
    after_delete.save(AssetCatalog::project_index(root));
    restarted.catalog_changed(std::make_shared<const AssetCatalog>(after_delete));
    check(restarted.queued() != 0, "Removed base asset did not invalidate its prior dependents");
    std::vector<AssetImportOutcome> missing;
    const auto removal_deadline = std::chrono::steady_clock::now() + 10s;
    do {
        for (auto& receipt : restarted.poll())
            missing.push_back(std::move(receipt));
        check(std::chrono::steady_clock::now() < removal_deadline,
              "Missing dependency admission stalled");
        std::this_thread::sleep_for(1ms);
    } while (restarted.queued() || !restarted.jobs().empty());
    check(!missing.empty() && !missing.back().published && !missing.back().diagnostic.empty() &&
              AssetCatalog::open_project(root).document() == after_delete.document(),
          "Removed dependency was silently ignored or erased its dependent's last-good selection");
    restarted.suspend(true);
    restarted.catalog_changed(std::make_shared<const AssetCatalog>(before_delete));
    check(restarted.queued() != 0, "Restored base did not queue its dependent");
    restarted.catalog_changed(std::make_shared<const AssetCatalog>(root));
    check(restarted.queued() == 0 && restarted.quiescent(),
          "Removed queued assets survived catalog reconciliation");
}
