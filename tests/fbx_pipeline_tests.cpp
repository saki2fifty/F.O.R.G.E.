#include "asset_bytes.hpp"
#include "asset_import_service.hpp"
#include "fbx_binary_fixture.hpp"
#include "fbx_fixture.hpp"
#include "fbx_importer.hpp"
#include "import_authoring.hpp"
#include "model_selection.hpp"
#include "source_copy.hpp"
#include "texture_encode.h"
#include <forge/material_asset.hpp>
#include <forge/model_asset.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    check(bool(stream.write(text.data(), std::streamsize(text.size()))) && bool(stream.flush()),
          "Cannot write original FBX fixture");
}
std::map<std::string, AssetId> source_ids(const ModelSelection& selection) {
    std::map<std::string, AssetId> result;
    for (const auto& member : selection.index.members)
        check(result
                  .emplace(member.identity.evidence.exporter_key,
                           selection.bindings.at(member.identity.address))
                  .second,
              "Duplicate FBX source identity");
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need worker, converter and test output directory");
        const auto worker = std::filesystem::absolute(argv[1]);
        const auto converter = std::filesystem::absolute(argv[2]);
        const auto root = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        const auto registry = asset_import_registry(worker, converter);
        check(fbx_recipe_revision() != model_recipe_revision(),
              "FBX and glTF lost separate recipe ownership");
        check(bool(registry->find(fbx_importer_id)),
              "FBX importer is missing from shared discovery");
        AssetImportService service(lease, registry, {"test", "none", "cpu"}, 1);
        const auto import = [&](const std::filesystem::path& source) {
            auto draft = service.prepare(source);
            check(draft.importer->descriptor().id == fbx_importer_id,
                  "FBX selected another importer");
            service.submit(
                std::move(draft),
                [](auto& candidate, const auto& plan, const auto& catalog) {
                    prepare_asset_publication(candidate, plan, catalog);
                },
                [](const auto&, const auto& artifact) {
                    (void)validate_model_bundle(artifact.files);
                });
            check(service.wait_idle(60s), "FBX import timed out");
            auto outcomes = service.poll();
            check(outcomes.size() == 1, "FBX import omitted its receipt");
            return std::move(outcomes.front());
        };
        const auto static_source = std::filesystem::path("Assets/triangle.fbx");
        write(root / static_source, test::fbx_triangle());
        auto first = import(static_source);
        check(first.published, first.diagnostic.c_str());
        const auto owner = first.job.asset;
        const auto selection = load_model_selection(root, first.publication->catalog, owner);
        check(selection.index.evidence_schema == fbx_evidence_schema &&
                  selection.index.members.size() == 3,
              "FBX publication did not retain the explicit source identity profile");
        const auto identities = source_ids(selection);
        write(root / static_source, test::fbx_triangle(true));
        auto reordered = import(static_source);
        check(reordered.published, reordered.diagnostic.c_str());
        check(source_ids(load_model_selection(root, reordered.publication->catalog, owner)) ==
                  identities,
              "FBX source object ordering changed durable asset identities");
        auto hit = import(static_source);
        check(hit.published && hit.cache_hit, "Unchanged FBX reimport did not reuse its artifact");
        for (const auto version : {7400u, 7500u}) {
            const auto source =
                std::filesystem::path("Assets/binary-" + std::to_string(version) + ".fbx");
            write(root / source, test::fbx_binary_triangle(version));
            auto binary = import(source);
            check(binary.published, binary.diagnostic.c_str());
            const auto selected =
                load_model_selection(root, binary.publication->catalog, binary.job.asset);
            const auto ids = source_ids(selected);
            write(root / source, test::fbx_binary_triangle(version, true));
            auto reordered_binary = import(source);
            check(reordered_binary.published, reordered_binary.diagnostic.c_str());
            check(source_ids(load_model_selection(root, reordered_binary.publication->catalog,
                                                  binary.job.asset)) == ids,
                  "Binary FBX record reordering changed durable IDs");
        }
        const auto before_bad_geometry = AssetCatalog::open_project(root).document();
        auto unsupported = test::fbx_triangle();
        unsupported.replace(unsupported.find("a:0,1,-3"), 8, "a:0,-2");
        write(root / static_source, unsupported);
        auto failed = import(static_source);
        check(!failed.published &&
                  AssetCatalog::open_project(root).document() == before_bad_geometry,
              "Failed FBX preparation replaced the last good selected family");
        for (bool skinned : {false, true}) {
            const auto source =
                std::filesystem::path(skinned ? "Assets/skinned.fbx" : "Assets/moving.fbx");
            write(root / source, test::fbx_animated_triangle(skinned));
            auto result = import(source);
            check(result.published, result.diagnostic.c_str());
            const auto selected =
                load_model_selection(root, result.publication->catalog, result.job.asset);
            const auto& animation = selected.index.hierarchy.at("animation");
            check(animation.at("clips").size() == 1 &&
                      selected.index.members.size() == (skinned ? 6 : 5),
                  "FBX clip/skeleton family is incomplete");
            const auto ids = source_ids(selected);
            check(ids.contains("fbx:clip:2000") && ids.contains("fbx:combined-rig"),
                  "FBX skeleton/clip IDs lost original source correspondence");
            std::filesystem::remove(root / source);
            const auto cooked =
                load_model_selection(root, result.publication->catalog, result.job.asset);
            check(cooked.index.hierarchy.at("animation") == animation,
                  "Source-hidden FBX selection depends on native source parsing");
        }
        const std::array<std::uint8_t, 16> pixels{255, 0, 0,   255, 0,   255, 0,   255,
                                                  0,   0, 255, 255, 255, 255, 255, 255};
        ForgeImageBytes png{};
        check(forge_png_encode_rgba8(pixels.data(), pixels.size(), 2, 2, 8, 1024 * 1024, &png),
              "Cannot encode original FBX test image");
        const std::string image(reinterpret_cast<const char*>(png.bytes), png.size);
        forge_image_bytes_free(&png);
        write(root / "Assets/color.png", image);
        write(root / static_source, test::fbx_textured_triangle());
        auto textured = import(static_source);
        check(textured.published, textured.diagnostic.c_str());
        const auto textured_selection =
            load_model_selection(root, textured.publication->catalog, owner);
        const auto textured_ids = source_ids(textured_selection);
        for (const auto& member : textured_selection.index.members)
            if (member.identity.type == "material") {
                const auto material = decode_material(textured_selection.bytes(member));
                const auto values = material_values_document(material);
                check(material.alpha == MaterialAlpha::Opaque &&
                          values.at("parameters").at("baseColorFactor").at("value")[3] == 1,
                      "FBX opaque material became transparent during shared cooking/publication");
            }
        check(textured_selection.index.members.size() == 5 &&
                  textured_ids.contains("fbx:material:4000") &&
                  textured_ids.contains("fbx:image:4001") &&
                  textured.publication->catalog.records().at(owner).source_dependencies.size() == 1,
              "FBX material/image were not published through the shared typed family");
        write(root / static_source, test::fbx_textured_triangle("../../outside.png"));
        const auto escaped = import(static_source);
        check(!escaped.published && AssetCatalog::open_project(root).document() ==
                                        textured.publication->catalog.document(),
              "Escaping FBX image path replaced the last good family");
        write(root / static_source, test::fbx_textured_triangle());
        const auto plan =
            prepare_source_copy(root, "Assets/Copied", {root / static_source}, {}, {},
                                [worker](const auto& source, auto stop) {
                                    return fbx_source_dependencies(worker, source, stop);
                                });
        check(plan.files.size() == 2 && plan.sources.size() == 1,
              "FBX source copy did not use supervised dependency admission");
        commit_source_copy(*lease, plan);
        auto copied = import(plan.destination / plan.sources[0]);
        check(copied.published && copied.job.asset != owner,
              "Copying raw FBX source reused the original asset identity");
        std::cout
            << "Supervised FBX static/rig/clip import, explicit provenance, stable source-order "
               "IDs, cache, last-good retention, source-hidden selection and raw copy passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
