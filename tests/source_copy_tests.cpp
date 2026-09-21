#include "../src/source_copy.hpp"
#include <forge/assets.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
static std::filesystem::path collision;
namespace forge {
void source_copy_before_rename() {
    if (!collision.empty()) {
        std::filesystem::create_directory(collision);
        collision.clear();
    }
}
} // namespace forge
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F&& f, std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid source copy accepted at " + std::to_string(where.line()));
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace
int main() {
    const auto root =
        std::filesystem::current_path() / ("source-copy-" + AssetId::generate().str());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{root};
    try {
        const auto project = root / "Project", input = root / "Input";
        std::filesystem::create_directories(project / "Assets");
        std::filesystem::create_directories(input / "textures");
        ProjectLease lease(project);
        atomic_write(input / "sample.png", "raw-image-fixture");
        atomic_write(input / "preview.bin", std::string(36, '\0'));
        const std::string model =
            R"({"asset":{"version":"2.0"},"buffers":[{"uri":"preview.bin","byteLength":36}],"images":[{"uri":"textures/albedo.png"}]})";
        atomic_write(input / "textures/albedo.png", "dependency-fixture");
        atomic_write(input / "model.gltf", model);
        const auto plan = prepare_source_copy(project, "Assets/Imported",
                                              {input / "model.gltf", input / "sample.png"});
        check(plan.sources.size() == 2 && plan.files.size() == 4 &&
                  !std::filesystem::exists(project / plan.destination),
              "Read-only source preparation wrote files or lost glTF dependencies");
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { commit_source_copy(lease, plan, stop.get_token()); });
        check(!std::filesystem::exists(project / plan.destination),
              "Cancelled copy published a folder");
        atomic_write(input / "textures/albedo.png", "changed-after-review");
        rejects([&] { commit_source_copy(lease, plan); });
        check(!std::filesystem::exists(project / plan.destination),
              "Stale glTF copy published partial input");
        atomic_write(input / "textures/albedo.png", "dependency-fixture");
        commit_source_copy(lease, plan);
        check(bytes(project / "Assets/Imported/Source-1/model.gltf") == model &&
                  bytes(project / "Assets/Imported/Source-1/textures/albedo.png") ==
                      "dependency-fixture" &&
                  bytes(project / "Assets/Imported/Source-2/sample.png") == "raw-image-fixture",
              "Source copy changed bytes or broke relative dependencies");
        check(!std::filesystem::exists(project / "forge.assets.json"),
              "Copy allocated a logical asset before import");
        rejects([&] { commit_source_copy(lease, plan); });
        check(bytes(project / "Assets/Imported/Source-1/model.gltf") == model,
              "Duplicate destination was overwritten");
        auto conflict = prepare_source_copy(project, "Assets/Conflict", {input / "sample.png"});
        std::filesystem::create_directory(project / "Assets/Conflict");
        rejects([&] { commit_source_copy(lease, conflict); });
        check(std::filesystem::is_empty(project / "Assets/Conflict"),
              "Existing empty folder was replaced");
        const auto racing = prepare_source_copy(project, "Assets/Race", {input / "sample.png"});
        collision = project / racing.destination;
        rejects([&] { commit_source_copy(lease, racing); });
        check(std::filesystem::is_empty(project / "Assets/Race"),
              "Destination created after preflight was replaced by rename");
        rejects([&] { prepare_source_copy(project, "../Escape", {input / "sample.png"}); });
        rejects([&] { prepare_source_copy(project, ".forge/Imported", {input / "sample.png"}); });
        rejects([&] {
            prepare_source_copy(project, "Assets/Duplicate",
                                {input / "sample.png", input / "sample.png"});
        });
        atomic_write(input / "sample.png.forge-import.json", "identity");
        rejects([&] {
            prepare_source_copy(project, "Assets/Identity",
                                {input / "sample.png.forge-import.json"});
        });
        atomic_write(input / "scene.scene.json", "identity");
        rejects(
            [&] { prepare_source_copy(project, "Assets/Identity", {input / "scene.scene.json"}); });
        atomic_write(
            input / "escape.gltf",
            R"({"asset":{"version":"2.0"},"buffers":[{"uri":"../private.bin","byteLength":1}]})");
        rejects([&] { prepare_source_copy(project, "Assets/Escape", {input / "escape.gltf"}); });
        auto invalid = prepare_source_copy(project, "Assets/Invalid", {input / "sample.png"});
        invalid.files.front().destination = "../escape.png";
        rejects([&] { commit_source_copy(lease, invalid); });
        check(!std::filesystem::exists(project / "Assets/Invalid"), "Invalid copy plan published");
#ifndef _WIN32
        std::filesystem::create_symlink(input / "sample.png", input / "linked.png");
        rejects([&] { prepare_source_copy(project, "Assets/Link", {input / "linked.png"}); });
#endif
        for (const auto& entry : std::filesystem::directory_iterator(project / "Assets"))
            check(!entry.path().filename().string().starts_with(".forge-import-"),
                  "Failed copy leaked ordinary owned staging");
        std::cout << "Source-copy bytes, dependencies, atomic folder, cancellation, "
                     "stale/duplicate/identity/path rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
