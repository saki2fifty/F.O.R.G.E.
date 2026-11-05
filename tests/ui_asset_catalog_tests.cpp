#include "asset_storage.hpp"
#include "editor/content_model.hpp"
#include "fixtures/texture_jpeg.hpp"
#include "fixtures/ui_png.hpp"
#include "ui_asset_catalog.hpp"
#include "ui_inspection_transport.hpp"
#include <forge/flecs_script.hpp>
#include <fstream>
#include <iostream>
#include <tuple>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid UI catalog operation accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected fixture directory and font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{root};
        ProjectLease writer(root);
        const std::string rml = "<rml><body><p>HUD</p></body></rml>";
        asset_storage::replace(root / "Assets/hud.rml", rml);
        asset_storage::replace(root / "Assets/hud.rcss", "body {color:red;}");
        std::filesystem::copy_file(argv[2], root / "Assets/font.ttf");
        auto document = register_ui_source(writer, "Assets/hud.rml");
        auto font = register_ui_source(writer, "Assets/font.ttf");
        check(document.type == "ui_document" && font.type == "ui_font" &&
                  register_ui_source(writer, "Assets/font.ttf").id == font.id,
              "UI source identity/type");
        UiResources admitted(root);
        admitted.document({document.id});
        admitted.read("Assets/hud.rcss");
        admitted.read("Assets/font.ttf");
        auto snapshot = admitted.snapshot();
        auto catalog = refresh_ui_asset_catalog(writer, snapshot);
        check(catalog.records().size() == 3 &&
                  catalog.records().at(document.id).dependency_edges.size() == 2,
              "UI source dependencies absent");
        check(catalog.dependency_graph().referrers(font.id) == std::vector<AssetId>{document.id},
              "Font referrer missing");
        check(catalog.dependency_graph().source_referrers("Assets/hud.rcss").size() == 2,
              "UI source invalidation index");
        SourceSnapshot sources;
        for (const auto& s : snapshot.sources)
            sources.files[s.source] = {s.source, s.digest, s.bytes, {}, "ui", {}};
        auto before = ContentIndex::build(catalog, &sources);
        ContentQuery changed;
        changed.state = ContentState::Changed;
        check(before.query(changed).empty(), "Fresh UI sources reported changed");
        sources.files["Assets/font.ttf"].digest = std::string(64, '0');
        check(ContentIndex::build(catalog, &sources).query(changed).size() == 2,
              "UI font change not reflected in shared Content status");
        const auto index = asset_storage::read(root / "forge.assets.json");
        check(!catalog.records()
                   .at(document.id)
                   .metadata.at("forge.ui_source")
                   .contains("runtime_features"),
              "Unfiltered UI acquired a runtime filter requirement");
        for (const auto& [extension, text, profile, gradient] :
             std::vector<std::tuple<std::string, std::string, unsigned, unsigned>>{
                 {"rcss", "body {filter:blur(1px);}", 1u, 0u},
                 {"rcss", "body {box-shadow:#0008 8px 6px 4px 0px;}", 5u, 0u},
                 {"rcss", "body {box-/**/shadow:none;}", 5u, 0u},
                 {"rcss", "/* box-shadow:#0008 8px 6px 4px; */ body {color:red;}", 0u, 0u},
                 {"rcss", "body {font-family:'box-shadow:8px 6px';}", 0u, 0u},
                 {"rml", "<rml><body data-style-box-shadow=\"effect\"></body></rml>", 5u, 0u},

                 {"rcss", "body {mask-image:linear-gradient(90deg,#0000,#ffff);}", 4u, 1u},
                 {"rcss", "body {mask-image:radial-gradient(circle,#0000,#ffff);}", 4u, 2u},
                 {"rcss", "body {mask-image:image(test.tga);}", 4u, 0u},
                 {"rcss", "body {mask-image:none;}", 4u, 0u},
                 {"rcss", "/* mask-image:linear-gradient(90deg,#0000,#ffff); */ body {color:red;}",
                  0u, 0u},
                 {"rcss", "body {font-family:'mask-image:linear-gradient(90deg,#0000,#ffff)';}", 0u,
                  0u},
                 {"rml", "<rml><body data-style-mask-image=\"effect\"></body></rml>", 4u, 2u},

                 {"rcss", "body {decorator:linear-gradient(90deg,#f00,#00f);}", 0u, 1u},
                 {"rcss",
                  "@decorator named : linear-gradient {angle:90deg;color-stops:#f00,#00f;} body "
                  "{decorator:named;}",
                  0u, 1u},
                 {"rcss", "body {decorator:repeating-linear-gradient(90deg,#f00,#00f);}", 0u, 1u},
                 {"rcss", "body {decorator:horizontal-gradient(#f00 #00f);}", 0u, 1u},
                 {"rcss", "body {decorator:radial-gradient(circle,#f00,#00f);}", 0u, 2u},
                 {"rcss", "body {decorator:repeating-radial-gradient(circle,#f00,#00f);}", 0u, 2u},
                 {"rcss", "body {decorator:conic-gradient(from 45deg,#f00,#00f);}", 0u, 2u},
                 {"rcss", "body {decorator:repeating-conic-gradient(#f00,#00f);}", 0u, 2u},
                 {"rcss",
                  "@decorator named : radial-gradient {color-stops:#f00,#00f;} body "
                  "{decorator:named;}",
                  0u, 2u},
                 {"rcss",
                  "@decorator named : conic-gradient {color-stops:#f00,#00f;} body "
                  "{decorator:named;}",
                  0u, 2u},
                 {"rcss", "/* decorator:radial-gradient(#f00,#00f); */ body {color:red;}", 0u, 0u},
                 {"rcss", "body {font-family:'conic-gradient(#f00,#00f)';}", 0u, 0u},
                 {"rcss", "body {decorator:image(test.tga);}", 0u, 0u},
                 {"rcss", "/* decorator:linear-gradient(90deg,#f00,#00f); */ body{color:red;}", 0u,
                  0u},
                 {"rcss", "body {font-family:'linear-gradient(90deg,#f00,#00f)';}", 0u, 0u},
                 {"rml", "<rml><body data-style-decorator=\"effect\"></body></rml>", 0u, 2u},
                 {"rcss", "body {backdrop-filter:blur(1px);}", 3u, 0u},
                 {"rcss", "body {backdrop-/**/filter:none;}", 3u, 0u},
                 {"rcss", "body {font-family:'backdrop-filter:blur(1px)';}", 0u, 0u},
                 {"rcss", "/* backdrop-filter:blur(1px); */ body {filter:blur(1px);}", 1u, 0u},
                 {"rml", "<rml><body data-style-backdrop-filter=\"effect\"></body></rml>", 3u, 0u},
                 {"rml",
                  "<rml><body data-style-backdrop-filter=\"effect\" "
                  "data-style-filter=\"other\"></body></rml>",
                  3u, 0u},
                 {"rml", "<rml><body style=\"backdrop-filter:blur(1px)\"></body></rml>", 3u, 0u},
                 {"rcss", "body {filter:drop-shadow(#123456 -4px 8px 2px);}", 2u, 0u},
                 {"rcss", "body {filter:drop-/**/shadow(#123456 -4px 8px 2px);}", 2u, 0u},
                 {"rcss", "/* filter:drop-shadow(red 1px 2px); */ body {color:red;}", 0u, 0u},
                 {"rcss", "body {font-family:'drop-shadow(red 1px 2px)';filter:blur(1px);}", 1u,
                  0u},
                 {"rcss", "body {fil/**/ter /* gap */ : opacity(0.5);}", 1u, 0u},
                 {"rcss", "/* filter:blur(3px); */ body {color:red;}", 0u, 0u},
                 {"rcss", "body {font-family:'filter:blur(3px)';}", 0u, 0u},
                 {"rml", "<rml><body style=\"filter:blur(1px)\"></body></rml>", 1u, 0u},
                 {"rml", "<rml><body style=\"fil&#116;er&#58;blur(2px)\"></body></rml>", 5u, 2u},
                 {"rml", "<rml><body style=\"color:&#114;ed\"></body></rml>", 5u, 2u},
                 {"rml", "<rml><head><style>body{filter:none;}</style></head><body/></rml>", 1u,
                  0u},
                 {"rml", "<rml><body data-style-filter=\"effect\"></body></rml>", 2u, 0u},
                 {"rml", "<rml><body><!-- filter:blur(2px); --></body></rml>", 0u, 0u},
                 {"rml", "<rml><body title=\"filter:blur(2px)\"></body></rml>", 0u, 0u}}) {
            const auto source = std::filesystem::path("Assets") / ("feature." + extension);
            asset_storage::replace(root / source, text);
            const auto feature = register_ui_source(writer, source);
            check(feature.metadata.at("forge.ui_source").contains("runtime_features") ==
                      (profile != 0 || gradient),
                  "UI feature evidence ignored declarations/bindings or interpreted comments/text");
            UiResources evidence(root);
            evidence.document({document.id});
            evidence.read(path_utf8(source));
            const auto observed = evidence.snapshot();
            const auto transported =
                ui_inspection_detail::decode(ui_inspection_detail::encode(observed));
            check(transported.sources == observed.sources,
                  "UI filter source evidence changed across inspection transport");
            if (profile || gradient) {
                nlohmann::json required = nlohmann::json::object();
                if (profile)
                    required["filters"] = profile;
                if (gradient)
                    required["gradients"] = gradient;
                check(feature.metadata.at("forge.ui_source").at("runtime_features") == required,
                      "UI source capability requirements differ from its actual declarations");
                if (profile)
                    check(feature.metadata.at("forge.ui_source")
                                  .at("runtime_features")
                                  .at("filters") == profile,
                          "UI filter declaration requires the wrong capability profile");
                auto forged = observed;
                for (auto& info : forged.sources)
                    info.details.erase("runtime_features");
                rejects([&] { refresh_ui_asset_catalog(writer, forged); });
            }
        }
        // Restore the catalog under test; feature fixtures have separate identities.
        asset_storage::replace(root / "forge.assets.json", index.value());
        auto bad = snapshot;
        bad.documents[document.id] = "Assets/hud.rcss";
        rejects([&] { refresh_ui_asset_catalog(writer, bad); });
        bad = snapshot;
        bad.sources.push_back(bad.sources.front());
        rejects([&] { refresh_ui_asset_catalog(writer, bad); });
        asset_storage::replace(root / "Assets/hud.rcss", "body {color:blue;}");
        rejects([&] { refresh_ui_asset_catalog(writer, snapshot); });
        asset_storage::replace(root / "Assets/font.ttf", "bad font");
        rejects([&] { register_ui_source(writer, "Assets/font.ttf"); });
        rejects([&] { register_ui_source(writer, "../escape.rml"); });
        check(asset_storage::read(root / "forge.assets.json") == index,
              "Rejected UI source changed prior catalog");
        std::vector<std::byte> tga(18 + 3, std::byte{});
        tga[2] = std::byte{2};
        tga[12] = tga[14] = std::byte{1};
        tga[16] = std::byte{24};
        tga[17] = std::byte{32};
        std::ofstream image(root / "Assets/image.tga", std::ios::binary);
        image.write(reinterpret_cast<const char*>(tga.data()), tga.size());
        image.close();
        auto texture = register_ui_source(writer, "Assets/image.tga");
        check(texture.type == "texture", "UI image invented a separate Texture identity type");
        catalog = AssetCatalog::open_project(root);
        texture.metadata["opaque.extension"] = {{"unchanged", true}};
        catalog.replace(texture);
        catalog.save(root / "forge.assets.json");
        const auto refreshed = register_ui_source(writer, "Assets/image.tga");
        check(refreshed.id == texture.id &&
                  refreshed.metadata.at("opaque.extension") ==
                      texture.metadata.at("opaque.extension") &&
                  refreshed.metadata.at("forge.ui_source").at("width") == 1,
              "UI image refresh lost Texture identity, extension data or dimensions");
        const std::array<unsigned char, 4> rgba{255, 64, 32, 128};
        const auto png = test::encode_ui_png(rgba.data(), 1, 1);
        for (const auto* extension : {"png", "jpg", "jpeg"}) {
            const auto path = std::filesystem::path("Assets") / (std::string("image.") + extension);
            const auto bytes = std::string_view(extension) == "png"
                                   ? std::span<const std::byte>(png)
                                   : std::as_bytes(std::span(texture_jpeg_fixture));
            asset_storage::replace(root / path,
                                   {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
            const auto registered = register_ui_source(writer, path);
            check(registered.type == "texture" &&
                      register_ui_source(writer, path).id == registered.id &&
                      registered.metadata.at("forge.ui_source").at("format") == extension,
                  "PNG/JPEG registration lost the common Texture identity or source format");
        }
        asset_storage::replace(root / "Assets/source.flecs", "Example {}\n");
        const auto script = register_flecs_script(root, "Assets/source.flecs");
        asset_storage::replace(root / "Assets/source.flecs", "Changed {}\n");
        const auto script_updated = register_flecs_script(root, "Assets/source.flecs");
        check(script.id == script_updated.id && script.source_dependencies.size() == 1 &&
                  script.source_dependencies[0].revision !=
                      script_updated.source_dependencies[0].revision,
              "Script root-source observation or stable identity was lost");
        std::cout << "UI catalog identity, admission, source status, graph and stale preservation "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
