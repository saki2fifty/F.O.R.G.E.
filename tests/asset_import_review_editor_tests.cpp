#define FORGE_UI_FIXTURE 1
#include "asset_bytes.hpp"
#include "asset_import_editor.hpp"
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
class ReviewImporter final : public AssetImporter {
    static AssetImporterDescriptor description() {
        AssetImporterDescriptor d;
        d.id = "test.review";
        d.revision = "1";
        d.label = "Review fixture";
        d.description = "Trusted CPU fixture for the real import editor";
        d.extensions = {".review"};
        d.source_kinds = {"test"};
        d.output_types = {"model", "mesh"};
        d.output_format = "test.review";
        d.targets = {{"*", "none", "cpu"}};
        d.execution = ImportExecution::TrustedCpuTask;
        return d;
    }

  public:
    ReviewImporter() : AssetImporter(description(), ImportSettingsSchema("test.review", 1, {})) {}
    ImportProbeResult probe(const ImportProbe&) const override {
        return {ImportProbeMatch::Strong, "test", "Review fixture"};
    }
    AssetImportPlan discover(const AssetImportRequest& r, std::stop_token) const override {
        AssetImportPlan plan;
        plan.input.source_digest =
            asset_detail::content_digest(asset_detail::read_bytes(r.project / r.source, 1024));
        plan.input.importer = descriptor().id;
        plan.input.importer_revision = descriptor().revision;
        plan.input.settings = settings().effective(r.settings);
        plan.input.output_format = descriptor().output_format;
        plan.input.platform = r.target.platform;
        plan.input.backend = r.target.backend;
        plan.input.profile = r.target.profile;
        return plan;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest&, const AssetImportPlan&, std::stop_token,
                    const std::function<void(double, std::string)>&) const override {
        return {{"review.bin", {std::byte{42}}}};
    }
    void validate(const CachedArtifact& a) const override {
        require(a.files.size() == 1 && a.files.front().name == "review.bin",
                "Invalid review fixture candidate");
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need owned scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::ofstream(root / "parts.review") << "Owned original review fixture";
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument document(scene);
        document.open_project(root, true);
        const std::vector<SubassetIdentityEntry> entries{
            {AssetId::generate(), "member/left", "mesh", "Hand", {}},
            {AssetId::generate(), "member/right", "mesh", "Hand", {}},
            {AssetId::generate(), "member/unnamed", "mesh", "", {}, true}};
        ImportEditorProfile profile;
        profile.id = "test.review";
        profile.title = "Import review";
        profile.target = {"portable", "none", "cpu"};
        profile.registry = [] {
            auto registry = std::make_shared<AssetImporterRegistry>();
            registry->add(std::make_shared<ReviewImporter>());
            registry->seal();
            return registry;
        };
        bool reviewed = false;
        profile.publish = [&](auto& c, const auto&, const auto&, auto choices) {
            if (!choices.empty()) {
                require(choices.size() == 1 && choices.front().address == "/meshes/0" &&
                            choices.front().previous == entries[1].id,
                        "Clicking a duplicate-name option selected another identity");
                reviewed = true;
            }
            if (!c.sidecar.identity.entries.empty() && !reviewed) {
                throw SubassetIdentityFailure({{"subasset.identity-ambiguous",
                                                "mesh",
                                                {"/meshes/0"},
                                                {entries[0].id, entries[1].id, entries[2].id},
                                                "Fixture requires explicit review"}});
            }
            c.sidecar.identity = {c.ticket.owner, c.ticket.source, c.input.source_digest,
                                  "test.review.v1", entries};
            c.records = {{c.ticket.owner, "model", c.ticket.source, 1, {}}};
            for (const auto& entry : entries) {
                AssetRecord record{entry.id, entry.type, c.ticket.source, 1, {}};
                record.subasset = AssetSubasset{c.ticket.owner, entry.key, entry.removed};
                c.records.push_back(std::move(record));
            }
        };
        AssetImportEditor editor(std::move(profile));
        test::observe_ui = true;
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        auto frame = [&] {
            test::ui_targets.clear();
            std::string message;
            editor.poll(document, message);
            ImGui::NewFrame();
            editor.draw(document, false);
            ImGui::Render();
        };
        auto finish = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do {
                frame();
                if (!editor.pending())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (std::chrono::steady_clock::now() < deadline);
            throw std::runtime_error("Review fixture import did not finish");
        };
        editor.open(document, "parts.review");
        editor.request_save();
        frame();
        finish();
        require(editor.diagnostic().empty(), editor.diagnostic().c_str());
        editor.request_save();
        frame();
        finish();
        require(!editor.identity_conflicts().empty(), "No identity review was presented");
        const auto left = editor.identity_option_label(entries[0].id);
        const auto right = editor.identity_option_label(entries[1].id);
        require(left.find("Hand | mesh | ") == 0 && right.find("Hand | mesh | ") == 0 &&
                    left != right && left.find(entries[0].id.str()) != std::string::npos &&
                    right.find(entries[1].id.str()) != std::string::npos,
                "Duplicate display names hide the distinct logical identities");
        const auto unnamed = editor.identity_option_label(entries[2].id);
        require(unnamed.find("member/unnamed | mesh | ") == 0 &&
                    unnamed.find("[removed]") != std::string::npos,
                "Unnamed/removed member review is not distinguishable");
        auto click = [&](const std::string& target) {
            frame();
            if (!test::ui_targets.contains(target)) {
                std::string available;
                for (const auto& [key, rect] : test::ui_targets)
                    available += key + "; ";
                throw std::runtime_error("Import review target missing: " + target +
                                         "; available: " + available);
            }
            const auto rect = test::ui_targets.at(target);
            require(rect.enabled && rect.minimum.y >= rect.clip_minimum.y &&
                        rect.maximum.y <= rect.clip_maximum.y,
                    "Import review target clipped or disabled");
            io.AddMousePosEvent((rect.minimum.x + rect.maximum.x) * .5f,
                                (rect.minimum.y + rect.maximum.y) * .5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        click("import-identity:/meshes/0");
        click("import-identity-option:" + entries[1].id.str());
        editor.request_save();
        frame();
        finish();
        require(reviewed && editor.diagnostic().empty() && editor.identity_conflicts().empty(),
                "Explicit identity option did not pass through the publication review");
        editor.request_save();
        reviewed = false;
        frame();
        finish();
        require(!editor.identity_conflicts().empty(), "No second review before project reset");
        std::filesystem::create_directory(root / "Other");
        document.open_project(root / "Other", true);
        frame();
        require(editor.identity_option_label(entries[0].id) == entries[0].id.str(),
                "Project switch retained another project's review labels");
        ImGui::DestroyContext();
        std::cout << "Import review labels, duplicate names, tombstones and project reset passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
