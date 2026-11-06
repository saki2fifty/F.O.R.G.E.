#include "../src/asset_bytes.hpp"
#include "../src/source_copy.hpp"
#include <forge/asset_publication.hpp>
#include <forge/assets.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace forge;
static std::filesystem::path collision;
static std::filesystem::path change_catalog;
static std::filesystem::path change_source;
static bool fail_after_publish = false;
namespace forge {
void source_copy_before_rename() {
    if (!change_source.empty()) {
        atomic_write(change_source, "external source at commit");
        change_source.clear();
    }
    if (!change_catalog.empty()) {
        atomic_write(change_catalog, "changed catalog at commit");
        change_catalog.clear();
    }
    if (!collision.empty()) {
        std::filesystem::create_directory(collision);
        collision.clear();
    }
}
void source_copy_after_publish() {
    if (fail_after_publish) {
        fail_after_publish = false;
        throw std::runtime_error("Simulated post-commit durability failure");
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
void source_revision_checks(const std::filesystem::path& root) {
    const auto project = root / "RevisionProject";
    std::filesystem::create_directories(project / "Assets");
    ProjectLease lease(project);
    const auto asset = AssetId::generate();
    const std::filesystem::path source = "Assets/Reflection.dds";
    const auto path = project / source;
    const auto sidecar_path = project / AssetPublisher::sidecar_path(source);
    const auto index = AssetCatalog::project_index(project);
    const std::string old_bytes("old\0source", 10), next_bytes("new\0source", 10);
    const auto digest = [](std::string_view value) {
        return asset_detail::content_digest(std::as_bytes(std::span(value)));
    };
    AssetImportSidecar sidecar;
    sidecar.settings = {"forge.texture.container", 1};
    sidecar.settings.overrides["max_size"] = 32;
    sidecar.identity.owner = asset;
    sidecar.identity.source = source;
    sidecar.identity.source_digest = digest(old_bytes);
    sidecar.identity.evidence_schema = "source-revision-test.v1";
    const auto sidecar_bytes = sidecar.document().dump();
    AssetRecord record{};
    record.id = asset;
    record.type = "texture";
    record.source = source;
    record.metadata["forge.import"] = {{"source_digest", digest(old_bytes)},
                                       {"sidecar_digest", digest(sidecar_bytes)},
                                       {"importer", "forge.texture.container"}};
    AssetCatalog catalog(project);
    catalog.add(record);
    const auto catalog_bytes = catalog.document().dump();
    atomic_write(index, catalog_bytes);
    atomic_write(sidecar_path, sidecar_bytes);
    atomic_write(path, old_bytes);
    const auto payload = std::as_bytes(std::span(next_bytes));
    const auto input =
        std::make_shared<const std::vector<std::byte>>(payload.begin(), payload.end());
    const auto plan =
        prepare_generated_source_revision(project, asset, input, digest(catalog_bytes));
    check(plan.revision && plan.revision->asset == asset && plan.files[0].generated == input &&
              bytes(path) == old_bytes,
          "Source revision preparation mutated bytes or lost identity/payload ownership");
    std::stop_source stop;
    stop.request_stop();
    rejects([&] { commit_source_copy(lease, plan, stop.get_token()); });
    check(bytes(path) == old_bytes, "Cancelled source revision overwrote source");
    atomic_write(index, catalog_bytes + " ");
    rejects([&] { commit_source_copy(lease, plan); });
    atomic_write(index, catalog_bytes);
    atomic_write(sidecar_path, sidecar_bytes + " ");
    rejects([&] { commit_source_copy(lease, plan); });
    atomic_write(sidecar_path, sidecar_bytes);
    atomic_write(path, "external draft");
    rejects(
        [&] { prepare_generated_source_revision(project, asset, input, digest(catalog_bytes)); });
    rejects([&] { commit_source_copy(lease, plan); });
    check(bytes(path) == "external draft", "Source revision discarded an external draft");
    atomic_write(path, old_bytes);
    auto invalid = plan;
    invalid.files[0].destination = "Other.dds";
    rejects([&] { commit_source_copy(lease, invalid); });
    invalid = plan;
    invalid.catalog_digest.clear();
    rejects([&] { commit_source_copy(lease, invalid); });
    invalid = plan;
    invalid.revision->asset = AssetId::generate();
    rejects([&] { commit_source_copy(lease, invalid); });
    invalid = plan;
    invalid.files[0].generated = std::make_shared<const std::vector<std::byte>>();
    invalid.files[0].bytes = invalid.bytes = 0;
    invalid.files[0].digest = digest("");
    rejects([&] { commit_source_copy(lease, invalid); });
    check(bytes(path) == old_bytes, "Empty generated revision discarded existing source");
    change_source = path;
    rejects([&] { commit_source_copy(lease, plan); });
    check(bytes(path) == "external source at commit", "Late source conflict was overwritten");
    atomic_write(path, old_bytes);
    check(commit_source_copy(lease, plan).diagnostic.empty() && bytes(path) == next_bytes &&
              bytes(index) == catalog_bytes && bytes(sidecar_path) == sidecar_bytes,
          "Source revision changed identity/settings/catalog or failed binary replacement");
    rejects(
        [&] { prepare_generated_source_revision(project, asset, input, digest(catalog_bytes)); });
    atomic_write(path, old_bytes);
    fail_after_publish = true;
    check(!commit_source_copy(lease, plan).diagnostic.empty() && bytes(path) == next_bytes,
          "Committed source bytes were reported as discarded after durability failure");
    atomic_write(path, old_bytes);
    const auto corrupt_sidecar = [&](AssetImportSidecar candidate) {
        const auto text = candidate.document().dump();
        auto changed = record;
        changed.metadata["forge.import"]["sidecar_digest"] = digest(text);
        auto corrupt = catalog;
        corrupt.replace(changed);
        const auto data = corrupt.document().dump();
        atomic_write(sidecar_path, text);
        atomic_write(index, data);
        rejects([&] { prepare_generated_source_revision(project, asset, input, digest(data)); });
        check(bytes(path) == old_bytes, "Inconsistent source identity changed the original bytes");
        atomic_write(index, catalog_bytes);
        atomic_write(sidecar_path, sidecar_bytes);
    };
    auto inconsistent = sidecar;
    inconsistent.identity.source_digest = digest("another revision");
    corrupt_sidecar(inconsistent);
    inconsistent = sidecar;
    SubassetIdentityEntry hidden{};
    hidden.id = AssetId::generate();
    hidden.key = "member/hidden";
    hidden.type = "texture";
    inconsistent.identity.entries.push_back(hidden);
    corrupt_sidecar(inconsistent);
    record.id = AssetId::generate();
    record.subasset = AssetSubasset{asset, "member/source-revision", false};
    catalog.add(record);
    const auto shared = catalog.document().dump();
    atomic_write(index, shared);
    rejects([&] { prepare_generated_source_revision(project, asset, input, digest(shared)); });
    check(bytes(path) == old_bytes, "Shared source revision mutated the original");
    std::cout << "Explicit source revision preserves identity/settings, rejects stale/shared/"
                 "cancelled input and reports committed bytes accurately\n";
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
        const std::vector<std::byte> generated_bytes{std::byte{0}, std::byte{1}, std::byte{255},
                                                     std::byte{0}};
        auto mutable_input = generated_bytes;
        const auto generated =
            prepare_generated_source_copy(project, "Assets/Atlas", "Atlas.png", mutable_input);
        mutable_input[0] = std::byte{99};
        check(!std::filesystem::exists(project / generated.destination),
              "Generated preparation wrote a source");
        commit_source_copy(lease, generated);
        check(bytes(project / "Assets/Atlas/Atlas.png") ==
                  std::string(reinterpret_cast<const char*>(generated_bytes.data()),
                              generated_bytes.size()),
              "Generated binary source did not use its immutable reviewed snapshot");
        check(!std::filesystem::exists(project / "forge.assets.json"),
              "Generated copy allocated a logical identity before import");
        auto shared_input = std::make_shared<const std::vector<std::byte>>(generated_bytes);
        const auto shared = prepare_shared_generated_source_copy(project, "Assets/SharedGenerated",
                                                                 "Reflection.dds", shared_input);
        check(shared.files.size() == 1 && shared.files[0].generated == shared_input,
              "Shared generated source copied or lost its owned payload");
        shared_input.reset();
        commit_source_copy(lease, shared);
        check(bytes(project / "Assets/SharedGenerated/Reflection.dds") ==
                  std::string(reinterpret_cast<const char*>(generated_bytes.data()),
                              generated_bytes.size()),
              "Shared source lost bytes after its producer released ownership");
        rejects([&] {
            prepare_shared_generated_source_copy(project, "Assets/NullGenerated", "Reflection.dds",
                                                 nullptr);
        });
        auto mutable_shared = std::make_shared<std::vector<std::byte>>(generated_bytes);
        const auto altered = prepare_shared_generated_source_copy(
            project, "Assets/ChangedSharedGenerated", "Reflection.dds", mutable_shared);
        (*mutable_shared)[0] = std::byte(99);
        rejects([&] { commit_source_copy(lease, altered); });
        check(!std::filesystem::exists(project / altered.destination),
              "Changed shared bytes bypassed the reviewed digest");
        rejects([&] {
            prepare_generated_source_copy(project, "Assets/BadGenerated", "../Atlas.png",
                                          generated_bytes);
        });
        rejects([&] {
            prepare_generated_source_copy(project, "Assets/BadGenerated", "forge.assets.json",
                                          generated_bytes);
        });
        rejects([&] {
            prepare_generated_source_copy(project, "Assets/BadGenerated", "Atlas.png", {});
        });
        auto ambiguous = prepare_generated_source_copy(project, "Assets/AmbiguousGenerated",
                                                       "Atlas.png", generated_bytes);
        ambiguous.files[0].original = input / "sample.png";
        rejects([&] { commit_source_copy(lease, ambiguous); });
        check(!std::filesystem::exists(project / ambiguous.destination),
              "Ambiguous generated/external source was published");
        const auto index = project / "forge.assets.json";
        const auto absent_digest = asset_detail::content_digest(std::span<const std::byte>{});
        auto absent = prepare_generated_source_copy(
            project, "Assets/InitiallyEmpty", "Reflection.dds", generated_bytes, absent_digest);
        commit_source_copy(lease, absent);
        check(!std::filesystem::exists(index) &&
                  std::filesystem::exists(project / absent.destination / "Reflection.dds"),
              "An absent catalog guard required a pre-existing asset index");
        auto appeared = prepare_generated_source_copy(
            project, "Assets/AppearedCatalog", "Reflection.dds", generated_bytes, absent_digest);
        atomic_write(index, "");
        rejects([&] { commit_source_copy(lease, appeared); });
        check(!std::filesystem::exists(project / appeared.destination),
              "An empty file appearing at the catalog path bypassed the absent-index guard");
        std::filesystem::remove(index);
        change_catalog = index;
        rejects([&] { commit_source_copy(lease, appeared); });
        check(!std::filesystem::exists(project / appeared.destination),
              "A catalog appearing during copy bypassed the absent-index guard");
        const std::string original_index = R"({"assets":[],"version":2})";
        atomic_write(index, original_index);
        const auto revision =
            asset_detail::content_digest(std::as_bytes(std::span(original_index)));
        auto guarded = prepare_generated_source_copy(project, "Assets/GuardedGenerated",
                                                     "Atlas.png", generated_bytes, revision);
        atomic_write(index, original_index + " ");
        rejects([&] { commit_source_copy(lease, guarded); });
        check(!std::filesystem::exists(project / guarded.destination),
              "Stale generated inputs published a source");
        atomic_write(index, original_index);
        change_catalog = index;
        rejects([&] { commit_source_copy(lease, guarded); });
        check(!std::filesystem::exists(project / guarded.destination) &&
                  bytes(index) == "changed catalog at commit",
              "Late input-catalog conflict published output or overwrote external bytes");
        for (const auto& entry : std::filesystem::directory_iterator(project / "Assets"))
            check(!entry.path().filename().string().starts_with(".forge-import-"),
                  "Failed copy leaked ordinary owned staging");
        source_revision_checks(root);
        std::cout << "Source-copy bytes, dependencies, atomic folder, cancellation, "
                     "stale/duplicate/identity/path rejection passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
