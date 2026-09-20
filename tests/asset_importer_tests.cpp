#include <forge/asset_importer.hpp>
#include <iostream>

using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid importer registration/selection accepted");
}
AssetImporterDescriptor descriptor(std::string id) {
    AssetImporterDescriptor result;
    result.id = id;
    result.revision = "1";
    result.label = id;
    result.description = "Test-only native importer implementation";
    result.extensions = {".fixture"};
    result.source_kinds = {"fixture"};
    result.output_types = {"fixture"};
    result.output_format = "forge.fixture";
    result.targets = {{"*", "none", "test"}};
    result.execution = ImportExecution::TrustedCpuTask;
    return result;
}
class Fixture final : public AssetImporter {
  public:
    explicit Fixture(AssetImporterDescriptor descriptor)
        : AssetImporter(descriptor, ImportSettingsSchema(descriptor.id, 1, {})) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return source.prefix.empty()
                   ? ImportProbeResult{}
                   : ImportProbeResult{ImportProbeMatch::Strong, "fixture", "Fixture byte present"};
    }
    AssetImportPlan discover(const AssetImportRequest&, std::stop_token) const override {
        throw std::runtime_error("Fixture has no production import plan");
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest&, const AssetImportPlan&, std::stop_token,
                    const std::function<void(double, std::string)>&) const override {
        throw std::runtime_error("Fixture is not a production importer");
    }
    void validate(const CachedArtifact&) const override {
        throw std::runtime_error("Fixture never validates a production artifact");
    }
};
} // namespace
int main() {
    try {
        const std::vector<std::byte> prefix{std::byte{42}};
        const ImportProbe probe{"Assets/item.FIXTURE", prefix};
        const ImportTarget target{"windows", "none", "test"};
        auto first = std::make_shared<Fixture>(descriptor("fixture.first"));
        AssetImporterRegistry registry;
        registry.add(first);
        rejects([&] { registry.candidates(probe, target); });
        rejects([&] { registry.add(std::make_shared<Fixture>(descriptor("fixture.first"))); });
        require(registry.descriptors().size() == 1, "Rejected registration mutated registry");
        registry.seal();
        require(registry.select(probe, target) == first && registry.find("fixture.first") == first,
                "Unique supported candidate was not selected");
        rejects([&] { registry.add(std::make_shared<Fixture>(descriptor("fixture.second"))); });
        rejects([&] { registry.select({"Assets/item.fixture", {}}, target); });
        rejects([&] { registry.select({"Assets/item.wrong", prefix}, target); });
        rejects([&] { registry.select(probe, {"windows", "d3d12", "test"}); });
        rejects([&] { registry.select(probe, target, "fixture.missing"); });
        std::vector<std::byte> huge(65537);
        rejects([&] { registry.candidates({"Assets/item.fixture", huge}, target); });
        AssetImporterRegistry both;
        auto second = std::make_shared<Fixture>(descriptor("fixture.second"));
        both.add(second);
        both.add(first);
        both.seal();
        const auto candidates = both.candidates(probe, target);
        require(candidates.size() == 2 && candidates[0].importer == first &&
                    candidates[1].importer == second,
                "Candidate order depends on registration order");
        rejects([&] { both.select(probe, target); });
        require(both.select(probe, target, "fixture.second") == second,
                "Explicit choice was ignored");
        auto bad = descriptor("fixture.bad");
        bad.extensions = {".UPPER"};
        rejects([&] {
            AssetImporterRegistry invalid;
            invalid.add(std::make_shared<Fixture>(bad));
        });
        bad = descriptor("fixture.bad");
        bad.limits.seconds = 0;
        rejects([&] {
            AssetImporterRegistry invalid;
            invalid.add(std::make_shared<Fixture>(bad));
        });
        bad = descriptor("fixture.bad");
        bad.targets = {{"*", "*", ""}};
        rejects([&] {
            AssetImporterRegistry invalid;
            invalid.add(std::make_shared<Fixture>(bad));
        });
        bad = descriptor("fixture.bad");
        bad.output_types = {"fixture", "fixture"};
        rejects([&] {
            AssetImporterRegistry invalid;
            invalid.add(std::make_shared<Fixture>(bad));
        });
        std::weak_ptr<const AssetImporter> weak;
        std::shared_ptr<const AssetImporter> selected;
        {
            AssetImporterRegistry owner;
            auto provider = std::make_shared<Fixture>(descriptor("fixture.owned"));
            weak = provider;
            owner.add(provider);
            owner.seal();
            selected = owner.select(probe, target);
        }
        require(!weak.expired(), "Selected job lost provider lifetime with registry owner");
        selected.reset();
        require(weak.expired(), "Registry selection leaked provider");
        std::cout << "Importer metadata, sealed registration, explicit ambiguity and provider "
                     "lifetime passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
