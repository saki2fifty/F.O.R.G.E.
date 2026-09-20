#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <source_location>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, std::source_location loc = std::source_location::current()) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid service operation accepted at " + std::to_string(loc.line()));
}
void write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    require(bool(f.write(text.data(), std::streamsize(text.size()))) && bool(f.flush()),
            "Fixture write");
}
class Importer final : public AssetImporter {
    static AssetImporterDescriptor descriptor() {
        AssetImporterDescriptor d;
        d.id = "test.image";
        d.revision = "1";
        d.label = "Test image";
        d.description = "Owned fixture";
        d.extensions = {".raw"};
        d.source_kinds = {"test"};
        d.output_types = {"texture"};
        d.output_format = "test.raw";
        d.targets = {{"*", "none", "cpu"}};
        d.execution = ImportExecution::TrustedCpuTask;
        return d;
    }

  public:
    Importer()
        : AssetImporter(descriptor(),
                        ImportSettingsSchema("test.image", 1,
                                             {{"gain", "Gain", "Fixture gain",
                                               ImportSettingType::Integer, 1, 0, 8}})) {}
    ImportProbeResult probe(const ImportProbe&) const override {
        return {ImportProbeMatch::Strong, "test", "Fixture"};
    }
    AssetImportPlan discover(const AssetImportRequest& r, std::stop_token) const override {
        AssetImportPlan p;
        p.input.source_digest =
            asset_detail::content_digest(asset_detail::read_bytes(r.project / r.source, 1024));
        p.input.importer = "test.image";
        p.input.importer_revision = "1";
        p.input.settings = settings().effective(r.settings);
        p.input.output_format = "test.raw";
        p.input.platform = r.target.platform;
        p.input.backend = r.target.backend;
        p.input.profile = r.target.profile;
        return p;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& r, const AssetImportPlan&, std::stop_token,
                    const std::function<void(double, std::string)>&) const override {
        return {{"image.bin", asset_detail::read_bytes(r.project / r.source, 1024)}};
    }
    void validate(const CachedArtifact& a) const override {
        require(a.files.size() == 1 && a.files[0].name == "image.bin" && !a.files[0].bytes.empty(),
                "Invalid raw fixture");
    }
};
void prepare(AssetPublicationCandidate& c, const AssetImportPlan&) {
    c.sidecar.identity.owner = c.ticket.owner;
    c.sidecar.identity.source = c.ticket.source;
    c.sidecar.identity.source_digest = c.input.source_digest;
    c.sidecar.identity.evidence_schema = "test.single.v1";
    c.records = {{c.ticket.owner, "texture", c.ticket.source, 1, {}}};
}
void compatible(const AssetCatalog&, const CachedArtifact&) {}
std::vector<AssetImportOutcome> finish(AssetImportService& service) {
    require(service.wait_idle(5s), "Import service stalled");
    return service.poll();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need test root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        write(root / "Assets/a.raw", "a");
        write(root / "Assets/b.raw", "b");
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(std::make_shared<Importer>());
        registry->seal();
        auto lease = std::make_shared<ProjectLease>(root);
        AssetImportService service(lease, registry, {"portable", "none", "cpu"});
        auto a = service.prepare("Assets/a.raw"), b = service.prepare("Assets/b.raw");
        const auto a_id = a.request.asset, b_id = b.request.asset;
        service.submit(a, prepare, compatible);
        service.submit(b, prepare, compatible);
        auto completed = finish(service);
        require(completed.size() == 2 && completed[0].published && completed[1].published,
                "Unrelated imports could not both publish");
        require(AssetCatalog::open_project(root).records().size() == 2,
                "Parallel import clobbered catalog");
        auto again = service.prepare("Assets/a.raw");
        require(again.request.asset == a_id, "Reimport changed identity");
        again.request.settings = again.importer->settings().edit(again.request.settings, "gain", 1);
        service.submit(again, prepare, compatible);
        completed = finish(service);
        require(completed.size() == 1 && completed[0].published && completed[0].cache_hit,
                "Equal-value intent did not reuse validated cache");
        auto settings = service.prepare("Assets/a.raw");
        require(settings.request.settings.overrides.at("gain") == 1, "Explicit equal setting lost");
        const auto baseline =
            asset_detail::read_bytes(root / "forge.assets.json", max_asset_index_bytes);
        service.submit(settings, prepare, [](const auto&, const auto&) {
            throw std::runtime_error("Incompatible fixture");
        });
        completed = finish(service);
        require(!completed[0].published &&
                    completed[0].diagnostic.find("Incompatible fixture") != std::string::npos,
                "Compatibility failure not surfaced");
        require(asset_detail::read_bytes(root / "forge.assets.json", max_asset_index_bytes) ==
                    baseline,
                "Failed import changed catalog");
        service.submit(service.prepare("Assets/a.raw"), prepare, compatible);
        require(service.wait_idle(5s), "Source stale fixture stalled");
        write(root / "Assets/a.raw", "changed");
        completed = service.poll();
        require(!completed[0].published, "Changed source published stale candidate");
        write(root / "Assets/a.raw", "a");
        auto stale = service.prepare("Assets/a.raw");
        auto sidecar = AssetImportSidecar::parse(*stale.ticket.sidecar_bytes);
        sidecar.unknown["vendor"] = nlohmann::json{{"preserve", true}};
        write(root / AssetPublisher::sidecar_path(stale.request.source), sidecar.document().dump());
        rejects([&] { service.submit(stale, prepare, compatible); });
        auto fresh = service.prepare("Assets/a.raw");
        auto cancelled = service.submit(fresh, prepare, compatible);
        require(service.wait_idle(5s), "Cancel fixture stalled");
        service.cancel(cancelled);
        completed = service.poll();
        require(!completed[0].published && completed[0].job.state == AssetJobState::Cancelled,
                "Cancelled prepared candidate published");
        service.submit(fresh, prepare, compatible);
        completed = finish(service);
        require(completed[0].published, "Valid unknown sidecar reimport failed");
        require(AssetImportSidecar::parse(*service.prepare("Assets/a.raw").ticket.sidecar_bytes)
                        .unknown.at("vendor")
                        .at("preserve") == true,
                "Unknown sidecar data lost");
        auto superseded = service.prepare("Assets/b.raw");
        service.submit(superseded, prepare, compatible);
        superseded.request.settings =
            superseded.importer->settings().edit(superseded.request.settings, "gain", 2);
        service.submit(superseded, prepare, compatible);
        completed = finish(service);
        require(completed.size() == 2 && completed[0].job.state == AssetJobState::Stale &&
                    !completed[0].published && completed[1].published,
                "Older settings generation published");
        require(service.prepare("Assets/b.raw").request.asset == b_id,
                "Settings changed persistent identity");
        auto changed_owner = service.prepare("Assets/a.raw");
        service.submit(changed_owner, prepare, compatible);
        require(service.wait_idle(5s), "Catalog stale fixture stalled");
        auto catalog = AssetCatalog::open_project(root);
        auto record = catalog.records().at(a_id);
        record.metadata["external"] = true;
        catalog.replace(record);
        catalog.save(root / "forge.assets.json");
        completed = service.poll();
        require(!completed[0].published, "Changed owner catalog was overwritten");
        auto wrong_thread = std::async(std::launch::async,
                                       [&] { rejects([&] { service.prepare("Assets/a.raw"); }); });
        wrong_thread.get();
        rejects([&] { service.prepare("../outside.raw"); });
        rejects([&] { ProjectLease second(root); });
        require(service.poll().empty(), "Receipts repeated");
        std::cout << "Shared asset service publication, cache, concurrency, stale/cancel and "
                     "ownership passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
