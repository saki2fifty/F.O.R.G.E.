#include "asset_bytes.hpp"
#include "fixtures/ui_font_collection.hpp"
#include "ui_asset_catalog.hpp"
#include "ui_font_profile.hpp"
#include "ui_markup_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <algorithm>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid collection accepted");
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    check(bool(stream), "Cannot write font fixture");
}
void admission(const std::vector<std::byte>& latin, const std::vector<std::byte>& arabic) {
    using namespace test;
    check(ui_font_detail::face_count(latin) == 1, "Standalone face changed");
    for (bool version2 : {false, true}) {
        const auto valid = font_collection({latin, arabic}, version2);
        check(ui_font_detail::face_count(valid) == 2, "Collection faces not admitted");
        validate_ui_font(valid);
        auto invalid = valid;
        for (const auto version : {0u, 0x00010001u, 0x00030000u}) {
            font_u32(invalid, 4, version);
            rejects([&] { validate_ui_font(invalid); });
        }
        for (const auto count : {0u, 33u, 0xffffffffu}) {
            invalid = valid;
            font_u32(invalid, 8, count);
            rejects([&] { validate_ui_font(invalid); });
        }
        for (const auto offset : {0u, 12u, 21u, std::uint32_t(valid.size()), 0xffffffffu}) {
            invalid = valid;
            font_u32(invalid, 16, offset);
            rejects([&] { validate_ui_font(invalid); });
        }
        invalid = valid;
        const auto directory = font_u32(valid, 16);
        font_u32(invalid, directory, 0);
        rejects([&] { validate_ui_font(invalid); });
        invalid = valid;
        font_u32(invalid, directory + 12 + 8, 0xfffffff0);
        rejects([&] { validate_ui_font(invalid); });
        invalid = valid;
        font_u32(invalid, directory + 12 + 12, 0xffffffff);
        rejects([&] { validate_ui_font(invalid); });
        for (const auto size :
             {std::size_t(0), std::size_t(11), std::size_t(19), valid.size() - 100})
            rejects([&] { validate_ui_font(std::span(valid).first(size)); });
        if (version2) {
            invalid = valid;
            font_u32(invalid, 20, 0x44534947);
            rejects([&] { validate_ui_font(invalid); });
            font_u32(invalid, 24, 8);
            font_u32(invalid, 28, std::uint32_t(valid.size()));
            invalid.resize(invalid.size() + 8);
            validate_ui_font(invalid); // Span admission does not authenticate a signature.
            font_u32(invalid, 28, 0xffffffff);
            rejects([&] { validate_ui_font(invalid); });
        }
    }
}
void native(const std::filesystem::path& root, const std::vector<std::byte>& latin,
            const std::vector<std::byte>& arabic, bool shaped) {
    std::filesystem::create_directories(root / "Assets");
    const auto collection = test::font_collection({latin, arabic}, shaped);
    auto padded = collection;
    padded.resize(ui_font_detail::file_byte_limit);
    write(root / "Assets/Faces.TTC", collection);
    write(root / "Assets/Faces.otc", collection);
    auto declarations =
        set_ui_stylesheet_font_face("", {}, "CollectionLatin", "Assets/Faces.TTC", 0);
    declarations =
        set_ui_stylesheet_font_face(declarations, {}, "CollectionArabic", "Assets/Faces.TTC", 1);
    std::ofstream(root / "Assets/Hud.rml") << "<rml><head><style>" << declarations << R"rml(
body {font-family:CollectionLatin;font-size:32px;color:#fff;}
p {display:block;} #arabic {font-family:CollectionArabic;-rmlui-direction:rtl;-rmlui-language:ar;}
</style></head><body><p>office</p><p id="arabic">سلام</p></body></rml>)rml";
    const auto document = register_ui_document(root, "Assets/Hud.rml");
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, latin, {}, nullptr, shaped);
        const auto observed = presenter.inspect_static({document.id});
        check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
        check(observed.sources.size() == 2 &&
                  observed.automatic_sources.contains("Assets/Faces.TTC"),
              "Native collection bypassed source observation/automatic closure");
        const auto source = std::find_if(observed.sources.begin(), observed.sources.end(),
                                         [](const auto& row) { return row.type == "ui_font"; });
        check(source != observed.sources.end() && source->details.at("faces") == 2 &&
                  source->details.at("runtime_features").at("font_collections") == 1,
              "Collection face count/kit requirement missing");
        ProjectLease lease(root);
        const auto catalog = refresh_ui_asset_catalog(lease, observed);
        const auto registered_font =
            std::find_if(catalog.records().begin(), catalog.records().end(),
                         [](const auto& row) { return row.second.type == "ui_font"; });
        check(registered_font != catalog.records().end(), "Collection has no UI font identity");
        const auto selected_face = ui_font_reference(catalog, registered_font->first, 1);
        check(selected_face.at("source") == "Assets/Faces.TTC" &&
                  selected_face.at("face_index") == 1 &&
                  selected_face.at("selected_face").at("index") == 1 &&
                  selected_face.at("details").at("faces") == 2,
              "Font resource selection lost its project-relative collection/face index");
        check(catalog.dependency_graph().source_referrers("Assets/Faces.TTC").size() == 2,
              "Collection file missing source dependency edges");
        const auto entity = EntityId::generate();
        presenter.reset("collections", 1);
        const Json snapshot{{"version", 1},
                            {"session", "collections"},
                            {"generation", 1u},
                            {"revision", 1u},
                            {"documents", Json::array({{{"entity", entity},
                                                        {"asset", document.id},
                                                        {"instance", entity.str()},
                                                        {"visible", true},
                                                        {"layer", 0u},
                                                        {"model", Json::object()},
                                                        {"commands", Json::array()}}})}};
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        check(presenter.diagnostic().empty() && renderer.draws && !renderer.textures.empty(),
              "Native indexed collection text did not produce glyph draws");
        auto* font = Rml::GetFontEngineInterface();
        const auto style = Rml::Style::FontStyle::Normal;
        const auto weight = Rml::Style::FontWeight::Normal;
        const auto load = [&](int index, const char* family) {
            return Rml::LoadFontFace(
                {reinterpret_cast<const Rml::byte*>(collection.data()), collection.size()}, family,
                style, weight, false, index);
        };
        check(load(0, "SameAlias") && load(1, "SameAlias"), "Indexed aliases failed");
        check(load(0, "SameAlias") && load(1, "SameAlias"), "Repeated face admission failed");
        check(Rml::LoadFontFace("Assets/Faces.otc", "OtherContainer", style, weight, false, 1) ==
                  false,
              "Font load outside candidate admission bypassed resource ownership");
        if (shaped) {
            check(!load(-1, "Invalid") && !load(2, "Invalid") && !load(65536, "Invalid"),
                  "Out-of-range or named-instance selector was accepted");
        }
        const auto latin_handle = font->GetFontFaceHandle("collectionlatin", style, weight, 32);
        const auto arabic_handle = font->GetFontFaceHandle("collectionarabic", style, weight, 32);
        check(latin_handle && arabic_handle && latin_handle != arabic_handle,
              "Different native collection faces collapsed");
        const Rml::String language = "en";
        const Rml::TextShapingContext context{language};
        check(font->GetStringWidth(latin_handle, "office", context) > 0 &&
                  font->GetStringWidth(arabic_handle, "office", context) > 0,
              "Indexed native face has no metrics");
        check(Rml::LoadFontFace({reinterpret_cast<const Rml::byte*>(arabic.data()), arabic.size()},
                                "ReferenceArabic", style, weight),
              "Reference standalone font failed to load");
        const auto reference = font->GetFontFaceHandle("referencearabic", style, weight, 32);
        const Rml::String arabic_language = "ar";
        Rml::TextShapingContext rtl{arabic_language};
        rtl.text_direction = Rml::Style::Direction::Rtl;
        check(reference && font->GetStringWidth(arabic_handle, "سلام", rtl) ==
                               font->GetStringWidth(reference, "سلام", rtl),
              "Collection selection disagrees with its original face metrics");
        if (shaped)
            for (int alias = 0; alias < 6; ++alias)
                check(Rml::LoadFontFace(
                          {reinterpret_cast<const Rml::byte*>(padded.data()), padded.size()},
                          "SharedCollection" + std::to_string(alias), style, weight, false,
                          alias % 2),
                      "Collection aliases duplicated retained source bytes");
    }
    check(renderer.textures.empty() && renderer.geometry.empty(),
          "Collection native resources survived shutdown");
    UiResources files(root);
    (void)files.read("Assets/Faces.otc");
    write(root / "Assets/Wrong.ttc", latin);
    rejects([&] { (void)files.read("Assets/Wrong.ttc"); });
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--fixture") {
            check(!std::filesystem::exists(argv[2]), "Choose a new font fixture path");
            const auto latin = forge::asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
            const auto arabic = forge::asset_detail::read_bytes(argv[4], 4 * 1024 * 1024);
            write(argv[2], forge::test::font_collection({latin, arabic}));
            return 0;
        }
        check(argc == 4, "Need scratch, Latin font and Arabic font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        const auto latin = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        const auto arabic = forge::asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        admission(latin, arabic);
        native(root / "default", latin, arabic, false);
        native(root / "shaped", latin, arabic, true);
        std::cout << "Font collection admission, native indexed faces, source closure and lifetime "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
