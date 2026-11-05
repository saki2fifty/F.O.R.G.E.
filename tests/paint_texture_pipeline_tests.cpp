#include "asset_file_operations.hpp"
#include "model_render_resource.hpp"
#include "paint_texture_copy.hpp"
#include "paint_texture_document.hpp"
#include "texture_authoring.hpp"
#include "texture_bundle_validation.hpp"
#include "texture_image_process.hpp"
#include "texture_importer.hpp"
#include <forge/texture_resource.hpp>
#include <iostream>
#include <source_location>

using namespace forge;
using namespace forge::asset_detail;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F action, std::source_location at = std::source_location::current()) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected paint pipeline rejection at " + std::to_string(at.line()));
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& worker, bool direct,
              TextureSemantic semantic) {
    const auto lease = std::make_shared<ProjectLease>(root);
    auto document =
        PaintTextureDocument::create(lease, "Paint.paint-texture.json", [&](AssetId id) {
            return PaintTextureSource{PaintTextureValue::create(id, 67, 35, semantic)};
        });
    const auto source_path = root / document->locator();
    const auto original = document->source();
    const auto baseline = read_bytes(source_path, paint_texture_source_byte_limit);
    PaintBrush brush;
    brush.color = {.5, .25, 1, .5};
    brush.radius = 4;
    brush.hardness = 1;
    const std::array<PaintPoint, 2> points{{{4.5, 8.5}, {58.5, 8.5}}};
    auto candidate = original;
    candidate.document = candidate.document.paint(points, brush);
    check(document->retains_history(candidate), "Normal stroke cannot retain Undo and Redo");
    document->edit_source(document->revision(), "Paint stroke",
                          [&](auto& source) { source = candidate; });
    const auto painted = document->source();
    const PaintPixel expected = semantic == TextureSemantic::Color ? PaintPixel{188, 137, 255, 128}
                                                                   : PaintPixel{128, 64, 255, 128};
    check(painted.document.pixel(20, 8) == expected &&
              original.document.pixel(20, 8) == PaintPixel{},
          "Paint document lost independent color/data or snapshot semantics");
    check(document->dirty() && document->can_undo() &&
              read_bytes(source_path, baseline.size()) == baseline,
          "Paint draft wrote disk or lost history");
    document->undo();
    check(document->source().document == original.document && !document->dirty(),
          "Stroke Undo failed");
    document->redo();
    check(document->source().document == painted.document, "Stroke Redo failed");
    const auto revision = document->revision();
    bool called = false;
    rejects([&] { document->edit_source(revision - 1, "Stale", [&](auto&) { called = true; }); });
    check(!called, "Stale paint invoked a mutation");
    for (const auto change : {0, 1, 2})
        rejects([&] {
            document->edit_source(revision, "Invalid canvas replacement", [&](auto& source) {
                source.document = PaintTextureValue::create(
                    change == 0 ? AssetId::generate() : source.asset(), change == 1 ? 68 : 67, 35,
                    change == 2 ? (semantic == TextureSemantic::Color ? TextureSemantic::Data
                                                                      : TextureSemantic::Color)
                                : semantic);
            });
        });
    check(document->revision() == revision && document->source().document == painted.document,
          "Rejected replacement changed the draft");
    const auto external =
        std::string(reinterpret_cast<const char*>(baseline.data()), baseline.size()) + " ";
    atomic_write(source_path, external);
    rejects([&] { document->save(); });
    check(document->dirty(), "Save conflict lost the paint draft");
    atomic_write(source_path,
                 std::string(reinterpret_cast<const char*>(baseline.data()), baseline.size()));
    document->save();
    PaintTextureDocument reopened(lease, document->locator());
    check(reopened.source().document == painted.document && !reopened.dirty(),
          "Saved paint did not reopen");
    document->undo();
    check(document->dirty(), "Undo after Save lost the saved boundary");
    document->redo();
    check(!document->dirty(), "Redo to saved paint remained dirty");

    const auto registry = texture_import_registry(worker);
    const auto bytes = read_bytes(source_path, paint_texture_source_byte_limit);
    const ImportTarget target{"portable", "none", "cpu"};
    const auto importer =
        registry->select({document->locator(), std::span(bytes).first(64)}, target);
    check(importer && importer->descriptor().id == paint_texture_importer_id,
          "Paint importer routing failed");
    check(registry->candidates({"unrelated.json", bytes}, target).empty(),
          "Paint importer claimed unrelated JSON");
    AssetImportRequest request{
        painted.asset(), root, document->locator(), target, {paint_texture_importer_id}};
    auto wrong = request;
    wrong.asset = AssetId::generate();
    rejects([&] { importer->discover(wrong, {}); });
    auto plan = importer->discover(request, {});
    auto cook = [&] {
        if (!direct)
            return importer->import_and_cook(request, plan, {}, {});
        ImportProcessRequest input{{{"recipe", paint_texture_importer_id},
                                    {"revision", texture_recipe_revision()},
                                    {"settings", request.settings},
                                    {"kind", plan.data.at("source_kind")},
                                    {"extension", plan.data.at("extension")},
                                    {"asset_id", plan.data.at("asset_id")},
                                    {"source_digest", plan.input.source_digest},
                                    {"backend", target.backend}},
                                   {{"source.bin", bytes}}};
        auto invalid = input;
        invalid.payload["asset_id"] = AssetId::generate().str();
        rejects([&] { execute_texture_recipe(invalid); });
        std::stop_source stop;
        stop.request_stop();
        rejects([&] { execute_texture_recipe(input, stop.get_token()); });
        return execute_texture_recipe(input);
    };
    auto files = cook();
    importer->validate({{}, nlohmann::json::object(), files});
    const auto cooked = decode_texture(files.at(0).bytes);
    check(cooked.semantic == semantic && cooked.width == 67 && cooked.height == 35 &&
              cooked.mips == 7 &&
              cooked.format == (semantic == TextureSemantic::Color ? TextureFormat::RGBA8Srgb
                                                                   : TextureFormat::RGBA8) &&
              cooked.subresources[0] == painted.document.texture().subresources[0] &&
              cooked.alpha == (semantic == TextureSemantic::Color ? TextureAlpha::Straight
                                                                  : TextureAlpha::Custom),
          "Paint cooking lost source pixels, color/data interpretation, alpha or mip chain");
    const auto repeated = cook();
    check(repeated.size() == files.size(), "Repeated paint recipe changed file count");
    for (std::size_t i = 0; i < files.size(); ++i)
        check(repeated[i].name == files[i].name && repeated[i].bytes == files[i].bytes,
              "Repeated paint recipe changed output");
    rejects([&] { importer->settings().edit(request.settings, "semantic", "normal"); });
    rejects([&] {
        importer->settings().edit(request.settings, "additional_usages",
                                  nlohmann::json::array({"hdr"}));
    });

    const auto text = painted.document.dump();
    const AssetRecord record{painted.asset(), "texture", document->locator(), 1, {}};
    const auto duplicate_id = AssetId::generate();
    const auto rewrite = project_asset_file_rewriter(root);
    const auto duplicate =
        rewrite(record, text, "Copy.paint-texture.json", {{painted.asset(), duplicate_id}});
    const auto copied = PaintTextureSource::parse(std::as_bytes(std::span(duplicate)));
    check(copied.asset() == duplicate_id &&
              copied.document.texture().subresources == painted.document.texture().subresources,
          "Reviewed paint source Duplicate lost new identity or exact pixels");
    check(rewrite(record, text, "Moved.paint-texture.json", {}) == text,
          "Paint source Move changed admitted source bytes");
    rejects([&] { rewrite(record, text, "Wrong.png", {}); });
    AssetPublisher publisher(*lease);
    AssetPublicationCandidate publication;
    publication.ticket = publisher.capture(request.asset, request.source);
    publication.input = plan.input;
    publication.sidecar.settings = request.settings;
    publication.sidecar.build_inputs = plan.input.document();
    publication.files = files;
    auto bad = plan;
    bad.data["asset_id"] = AssetId::generate().str();
    rejects([&] { prepare_texture_publication(publication, bad); });
    prepare_texture_publication(publication, plan);
    auto result =
        publisher.publish(std::move(publication), *importer, [](const auto&, const auto&) {});
    auto selection = load_texture_selection(root, result.catalog,
                                            AssetRef<TextureAsset>{request.asset}, semantic);
    check(selection.value->subresources == cooked.subresources &&
              selection.value->semantic == semantic,
          "Published paint is not consumable by the ordinary Texture resource owner");
    const auto pixels =
        load_paint_texture_pixels(root, result.catalog, {request.asset}, semantic, worker);
    check(document->source().document.replace_pixels(pixels).texture().subresources[0] ==
              cooked.subresources[0],
          "Published pixel copy changed exact color/data base pixels");
    rejects([&] {
        load_paint_texture_pixels(root / "foreign", result.catalog, {request.asset}, semantic,
                                  worker);
    });
    rejects([&] {
        load_paint_texture_pixels(root, result.catalog, {request.asset}, TextureSemantic::Normal,
                                  worker);
    });
    if (!direct) {
        const auto variant = [&](ImportSettingsDocument settings) {
            auto changed = request;
            changed.settings = std::move(settings);
            const auto prepared = importer->discover(changed, {});
            AssetPublicationCandidate output;
            output.ticket = publisher.capture(changed.asset, changed.source);
            output.input = prepared.input;
            output.sidecar.settings = changed.settings;
            output.sidecar.build_inputs = prepared.input.document();
            output.files = importer->import_and_cook(changed, prepared, {}, {});
            prepare_texture_publication(output, prepared);
            return publisher.publish(std::move(output), *importer, [](const auto&, const auto&) {});
        };
        auto compressed = variant(importer->settings().edit(request.settings, "compression", "bc"));
        const auto block =
            load_texture_selection(root, compressed.catalog, {request.asset}, semantic);
        check(texture_format_info(block.value->format).compressed,
              "Copy fixture did not cook native blocks");
        const auto decoded =
            std::get<Rgba8Pixels>(texture_compressed_pixels(worker, root, *block.value));
        const auto copied_blocks =
            load_paint_texture_pixels(root, compressed.catalog, {request.asset}, semantic, worker);
        check(copied_blocks.width == 67 && copied_blocks.height == 35 &&
                  copied_blocks.block_decoded && copied_blocks.rgba == decoded.rgba &&
                  copied_blocks.srgb == decoded.srgb,
              "Paint copy differs from the existing isolated block sample projection");
        if (semantic == TextureSemantic::Color) {
            auto premultiplied =
                variant(importer->settings().edit(request.settings, "premultiply", true));
            rejects([&] {
                load_paint_texture_pixels(root, premultiplied.catalog, {request.asset}, semantic,
                                          worker);
            });
        }
    }
    const auto before_save = AssetCatalog::open_project(root);
    const auto selected_before_save =
        load_texture_selection(root, before_save, {request.asset}, semantic).value->subresources;
    document->edit_source(document->revision(), "Fill", [](auto& source) {
        source.document = source.document.fill({11, 22, 33, 44});
    });
    document->save();
    rejects([&] { importer->import_and_cook(request, plan, {}, {}); });
    check(load_texture_selection(root, AssetCatalog::open_project(root),
                                 AssetRef<TextureAsset>{request.asset}, semantic)
                  .value->subresources == selected_before_save,
          "Unpublished paint Save replaced the selected cooked revision");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || argc == 4, "Need --direct root or --worker executable root");
        const bool direct = std::string_view(argv[1]) == "--direct";
        check((direct && argc == 3) ||
                  (!direct && argc == 4 && std::string_view(argv[1]) == "--worker"),
              "Invalid mode");
        const auto root = std::filesystem::absolute(argv[argc - 1]) / AssetId::generate().str();
        for (const auto semantic : {TextureSemantic::Color, TextureSemantic::Data}) {
            const auto child = root / (semantic == TextureSemantic::Color ? "Color" : "Data");
            std::filesystem::create_directories(child);
            exercise(child, direct ? std::filesystem::path{} : std::filesystem::absolute(argv[2]),
                     direct, semantic);
        }
        std::filesystem::remove_all(root);
        std::cout << "Paint source history/conflict/native recipe/publication tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
