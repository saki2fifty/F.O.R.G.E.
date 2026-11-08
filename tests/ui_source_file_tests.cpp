#include "project_text_source.hpp"
#include <forge/project_lease.hpp>
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid raw source operation accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        using namespace forge;
        check(argc == 2, "Need test scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        std::filesystem::create_directories(root / "Scenes");
        ProjectLease writer(root);
        writer.check();
        const auto path = ProjectTextSource::path(root, "Assets/Hud.rml");
        const std::string initial = "<!-- retain -->\r\n<rml><body><p>First</p></body></rml>\r\n";
        check(!ProjectTextSource::read(path), "Missing source returned bytes");
        ProjectTextSource::validate(path, initial);
        ProjectTextSource::write_validated(path, {}, initial);
        check(ProjectTextSource::read(path) == initial, "Raw source storage normalized bytes");
        const auto baseline = ProjectTextSource::read(path);
        const std::string candidate = "<rml><body><p>Edited</p></body></rml>";
        ProjectTextSource::validate(path, candidate);
        ProjectTextSource::write_validated(path, baseline, candidate);
        check(ProjectTextSource::read(path) == candidate, "Valid source failed publication");
        rejects([&] { ProjectTextSource::write_validated(path, baseline, initial); });
        check(ProjectTextSource::read(path) == candidate, "Stale source replaced current bytes");
        rejects([&] { ProjectTextSource::validate(path, "<rml><body>"); });
        rejects([&] { ProjectTextSource::validate(path, std::string(1, '\0')); });
        rejects([&] { ProjectTextSource::validate(path, std::string(300000, 'x')); });
        check(ProjectTextSource::read(path) == candidate, "Validation failure changed disk bytes");
        const auto missing_parent = ProjectTextSource::path(root, "UI/Missing.rml");
        rejects([&] { ProjectTextSource::write_validated(missing_parent, {}, candidate); });
        check(!std::filesystem::exists(root / "UI"),
              "Write created an undeclared source directory");
        for (const auto* locator :
             {"Scenes/main.rml", "Native/gameplay.cpp", "Assets/catalog.json"})
            rejects([&] { (void)ProjectTextSource::path(root, locator); });
        check(!ProjectTextSource::allowed_root({}), "Empty locator has a source owner");
        std::error_code error;
        std::filesystem::create_symlink(path, root / "Assets/Alias.rml", error);
        if (!error)
            rejects([&] { (void)ProjectTextSource::path(root, "Assets/Alias.rml"); });
        std::cout << "Raw source bytes, admission, baseline conflicts and failed-save preservation "
                     "passed; symlink test "
                  << (error ? "unavailable" : "passed") << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
