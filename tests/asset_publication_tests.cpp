#include "asset_bytes.hpp"
#include <cstdlib>
#include <forge/asset_publication.hpp>
#include <forge/engine_assets.hpp>
#include <forge/scene.hpp>
#include <future>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace forge;
namespace {
unsigned crash_stage = 0;
std::stop_source* cancel_stage = nullptr;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid publication accepted");
}
std::string read(const std::filesystem::path& path) {
    const auto bytes = asset_detail::read_bytes(path, 256 * 1024 * 1024);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
AssetImporterDescriptor fixture_descriptor() {
    AssetImporterDescriptor d;
    d.id = "fixture.publication";
    d.revision = "1";
    d.label = "Publication fixture";
    d.description = "Admission fixture, not a production mesh decoder";
    d.extensions = {".fixture"};
    d.source_kinds = {"fixture"};
    d.output_types = {"model", "mesh"};
    d.output_format = "forge.publication-fixture";
    d.targets = {{"*", "none", "test"}};
    d.execution = ImportExecution::TrustedCpuTask;
    return d;
}
class FixtureImporter final : public AssetImporter {
  public:
    FixtureImporter()
        : AssetImporter(fixture_descriptor(),
                        ImportSettingsSchema("fixture.publication", 1,
                                             {{"enabled", "Enabled", "Fixture setting",
                                               ImportSettingType::Boolean, true}})) {}
    ImportProbeResult probe(const ImportProbe&) const override { return {}; }
    AssetImportPlan discover(const AssetImportRequest&, std::stop_token) const override {
        throw std::runtime_error("Fixture only");
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest&, const AssetImportPlan&, std::stop_token,
                    const std::function<void(double, std::string)>&) const override {
        throw std::runtime_error("Fixture only");
    }
    void validate(const CachedArtifact& a) const override {
        require(a.files.size() == 1 && a.files[0].name == "mesh.bin" &&
                    a.files[0].bytes == std::vector<std::byte>{std::byte{42}},
                "Invalid fixture artifact");
    }
};
struct Fixture {
    std::filesystem::path root;
    ProjectLease lease;
    AssetPublisher publisher;
    FixtureImporter importer;
    AssetId owner = AssetId::generate(), member = AssetId::generate();
    explicit Fixture(std::filesystem::path path)
        : root(std::move(path)), lease(root), publisher(lease) {
        std::filesystem::create_directories(root / "Assets");
    }
    AssetPublicationCandidate candidate(std::string text = "first") {
        const std::filesystem::path source = "Assets/source.fixture";
        atomic_write(root / source, text);
        AssetPublicationCandidate c;
        c.ticket = publisher.capture(owner, source);
        c.input.source_digest = asset_detail::content_digest(std::as_bytes(std::span(text)));
        c.input.importer = importer.descriptor().id;
        c.input.settings = importer.settings().defaults();
        c.input.importer_revision = "1";
        c.input.output_format = importer.descriptor().output_format;
        c.input.platform = "portable";
        c.input.backend = "none";
        c.input.profile = "test";
        c.sidecar.settings.importer = c.input.importer;
        c.sidecar.identity = {owner,
                              source,
                              c.input.source_digest,
                              "fixture-1",
                              {{member, "member/stable", "mesh", "Geometry", {}}}};
        c.sidecar.build_inputs = c.input.document();
        c.records = {{owner, "model", source, 1, {}}, {member, "mesh", source, 1, {}}};
        c.records[1].subasset = AssetSubasset{owner, "member/stable", false};
        c.files = {{"mesh.bin", {std::byte{42}}}};
        return c;
    }
    AssetPublicationResult publish(AssetPublicationCandidate c,
                                   AssetPublisher::Compatibility check = {},
                                   std::stop_token stop = {}) {
        if (!check)
            check = [](const AssetCatalog&, const CachedArtifact&) {};
        return publisher.publish(std::move(c), importer, check, stop);
    }
    auto state() const {
        return std::pair{read(root / "forge.assets.json"),
                         read(root / "Assets/source.fixture.forge-import.json")};
    }
};
void unit(const std::filesystem::path& root) {
    {
        std::filesystem::create_directories(root / "initial-cancel");
        Fixture first_import(root / "initial-cancel");
        std::stop_source cancelled;
        cancel_stage = &cancelled;
        rejects([&] { first_import.publish(first_import.candidate(), {}, cancelled.get_token()); });
        cancel_stage = nullptr;
        require(!std::filesystem::exists(first_import.root / "forge.assets.json") &&
                    !std::filesystem::exists(first_import.root /
                                             "Assets/source.fixture.forge-import.json") &&
                    !first_import.publisher.recover(),
                "Cancelled first import left selected metadata");
    }
    Fixture f(root);
    auto first = f.publish(f.candidate());
    require(first.cleanup_diagnostic.empty(), "Unexpected publication cleanup error");
    auto baseline = f.state();
    auto reopened = AssetCatalog::open_project(root);
    require(reopened.document() == first.catalog.document(), "Catalog reopen lost selection");
    const auto mapping = AssetImportSidecar::parse(baseline.second);
    require(mapping.identity.owner == f.owner && mapping.identity.entries[0].id == f.member,
            "Mapping changed on disk");
    require(!f.publisher.recover(), "Completed publication retained a journal");
    auto fail = [&](auto edit) {
        auto c = f.candidate();
        edit(c);
        rejects([&] { f.publish(std::move(c)); });
        require(f.state() == baseline, "Rejected candidate changed selected catalog/sidecar");
    };
    fail([](auto& c) { c.files[0].bytes[0] = std::byte{0}; });
    fail([](auto& c) { c.input.importer_revision = "different"; });
    fail([](auto& c) { c.sidecar.identity.entries[0].key = "remapped"; });
    fail([](auto& c) { c.records.erase(c.records.begin() + 1); });
    fail([](auto& c) { c.records[1].type = "material"; });
    fail([](auto& c) { c.records[0].source = "../outside"; });
    fail([&](auto& c) { atomic_write(root / c.ticket.source, "changed"); });
    for (const bool wrong_type : {false, true})
        fail([&](auto& c) {
            const auto texture = engine_texture(EngineTexture::White);
            c.input.dependencies = {
                {texture.id, wrong_type ? MaterialAsset::type : TextureAsset::type,
                 AssetDependencyKind::Runtime, "fixture.texture",
                 wrong_type ? engine_asset_revision(texture.id) : std::string(64, 'a')}};
            c.sidecar.build_inputs = c.input.document();
        });
    rejects([&] {
        f.publish(f.candidate(), [](const auto&, const auto&) {
            throw std::runtime_error("Incompatible live resource");
        });
    });
    require(f.state() == baseline, "Compatibility failure changed selection");
#ifdef _WIN32
    const auto index_path = root / "forge.assets.json";
    const HANDLE blocker = CreateFileW(index_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(blocker != INVALID_HANDLE_VALUE, "Cannot hold Windows replacement fixture");
    try {
        rejects([&] { f.publish(f.candidate("blocked-write")); });
    } catch (...) {
        CloseHandle(blocker);
        throw;
    }
    CloseHandle(blocker);
    require(f.state() == baseline && !f.publisher.recover(),
            "Blocked Windows replacement lost previous selection");
#endif
    auto c = f.candidate();
    std::stop_source stopped;
    stopped.request_stop();
    rejects([&] { f.publish(c, {}, stopped.get_token()); });
    std::stop_source during;
    cancel_stage = &during;
    auto intent_only = f.candidate();
    intent_only.sidecar.settings.overrides["enabled"] = true; // Equal to effective default.
    require(intent_only.input.key() == first.artifact.key, "Equal intent changed content identity");
    rejects([&] { f.publish(std::move(intent_only), {}, during.get_token()); });
    cancel_stage = nullptr;
    require(f.state() == baseline && !f.publisher.recover(),
            "Cancellation did not roll back sidecar");
    auto stale = f.candidate();
    atomic_write(root / "forge.assets.json", baseline.first + "\n");
    rejects([&] { f.publish(stale); });
    require(read(root / "forge.assets.json") == baseline.first + "\n",
            "External catalog edit overwritten");
    atomic_write(root / "forge.assets.json", baseline.first);
    atomic_write(root / "Assets/source.fixture.forge-import.json", baseline.second + "\n");
    rejects([&] { f.publish(stale); });
    atomic_write(root / "Assets/source.fixture.forge-import.json", baseline.second);
    rejects([&] {
        f.publish(f.candidate(), [&](const auto&, const auto&) {
            atomic_write(root / "Assets/source.fixture", "late change");
        });
    });
    require(f.state() == baseline, "Source changed during compatibility was selected");
    auto wrong_thread = std::async(std::launch::async, [&] {
        rejects([&] { f.publisher.capture(f.owner, "Assets/source.fixture"); });
    });
    wrong_thread.get();
    for (auto path : {"forge.assets.json", "FORGE.ASSETS.JSON", "forge.assets.json.v1.backup",
                      ".FoRgE/cache.fixture", "Assets/source.FORGE-IMPORT.JSON"})
        rejects([&] { f.publisher.capture(f.owner, path); });
    std::filesystem::create_hard_link(root / "forge.assets.json", root / "Assets/control.fixture");
    rejects([&] { f.publisher.capture(f.owner, "Assets/control.fixture"); });
    std::filesystem::remove(root / "Assets/control.fixture");
    // Required revision must be selected, not just an existing UUID/type.
    c = f.candidate();
    c.input.dependencies.push_back({AssetId::generate(), "texture", AssetDependencyKind::Build,
                                    "albedo", std::string(64, 'a')});
    c.sidecar.build_inputs = c.input.document();
    rejects([&] { f.publish(c); });
    // A blocked catalog staging/replace must preserve previous metadata. Using
    // an incompatible existing source-controlled migration backup tests precommit I/O failure.
    auto legacy = Json::parse(baseline.first);
    legacy["version"] = 1;
    atomic_write(root / "forge.assets.json", legacy.dump());
    atomic_write(root / "forge.assets.json.v1.backup", "unrelated backup");
    rejects([&] { f.publish(f.candidate()); });
    require(read(root / "forge.assets.json") == legacy.dump(), "Failed backup replaced v1 catalog");
    std::filesystem::remove(root / "forge.assets.json.v1.backup");
    auto second = f.publish(f.candidate("second"));
    require(read(root / "forge.assets.json.v1.backup") == legacy.dump(),
            "Migration backup missing");
    require(second.artifact.key != first.artifact.key, "Changed source retained stale revision");
    require(second.catalog.members(f.owner) == std::vector<AssetId>{f.member},
            "Reimport changed logical member identity");
    c = f.candidate("removed");
    c.sidecar.identity.entries[0].removed = true;
    c.records[1].subasset->removed = true;
    auto removed = f.publish(std::move(c));
    require(removed.catalog.resolve(f.member, "mesh").state == AssetState::Removed,
            "Removed member not diagnosed");
    DerivedDataCache cache(root);
    require(cache.find(f.candidate("first").input, [&](const auto& a) { f.importer.validate(a); })
                .has_value(),
            "Previous good artifact was destroyed");
}
} // namespace
namespace forge {
void asset_publication_test_checkpoint(unsigned stage) {
    if (crash_stage == stage)
        std::_Exit(90 + stage);
    if (cancel_stage && stage == 2)
        cancel_stage->request_stop();
}
} // namespace forge
int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 4, "Need scratch root and optional crash/recover stage");
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        if (argc == 2) {
            unit(root);
            std::cout << "Asset candidate publication/last-good/stale/ownership checks passed\n";
            return 0;
        }
        const auto stage = static_cast<unsigned>(std::stoul(argv[3]));
        Fixture f(root);
        if (std::string(argv[2]) == "--crash") {
            f.publish(f.candidate());
            const auto old = f.state();
            atomic_write(root / "expected.json",
                         Json{{"catalog", old.first}, {"sidecar", old.second}}.dump());
            crash_stage = stage;
            f.publish(f.candidate("new-source"));
            throw std::runtime_error("Crash checkpoint not reached");
        }
        require(std::string(argv[2]) == "--recover", "Unknown test mode");
        const auto before = f.state();
        require(f.publisher.recover(), "Crash recovery journal missing");
        const auto after = f.state();
        const auto expected = Json::parse(read(root / "expected.json"));
        if (stage < 3)
            require(after == std::pair{expected.at("catalog").get<std::string>(),
                                       expected.at("sidecar").get<std::string>()},
                    "Interrupted precommit did not restore last-good metadata");
        else
            require(after == before && after.first != expected.at("catalog").get<std::string>(),
                    "Postcommit recovery rolled back successful selection");
        require(!f.publisher.recover(), "Recovery is not idempotent");
        std::cout << "Process interruption recovery passed at stage " << stage << "\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
