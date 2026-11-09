#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "fixtures/ui_font_collection.hpp"
#include "ui_font_profile.hpp"
#include "ui_inspection_transport.hpp"
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
using Data = std::vector<std::byte>;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void u16(Data& bytes, std::size_t at, unsigned value) {
    bytes.at(at) = std::byte(value >> 8);
    bytes.at(at + 1) = std::byte(value & 255);
}
Data unicode(std::initializer_list<unsigned> units) {
    Data result(units.size() * 2);
    std::size_t at = 0;
    for (auto value : units) {
        u16(result, at, value);
        at += 2;
    }
    return result;
}
Data ascii(std::string_view text, bool utf16 = true) {
    Data result;
    for (unsigned char c : text) {
        if (utf16)
            result.push_back(std::byte{});
        result.push_back(std::byte(c));
    }
    return result;
}
struct Name {
    unsigned platform, encoding, language, id;
    Data text;
};
Data font(std::initializer_list<Name> records, std::initializer_list<Data> tags = {}) {
    using forge::test::font_u32;
    const auto storage = 6 + records.size() * 12 + (tags.size() ? 2 + tags.size() * 4 : 0);
    Data names(storage);
    u16(names, 0, tags.size() ? 1 : 0);
    u16(names, 2, unsigned(records.size()));
    u16(names, 4, unsigned(storage));
    std::size_t at = 6;
    for (const auto& value : records) {
        u16(names, at, value.platform);
        u16(names, at + 2, value.encoding);
        u16(names, at + 4, value.language);
        u16(names, at + 6, value.id);
        u16(names, at + 8, unsigned(value.text.size()));
        u16(names, at + 10, unsigned(names.size() - storage));
        names.insert(names.end(), value.text.begin(), value.text.end());
        at += 12;
    }
    if (tags.size()) {
        u16(names, at, unsigned(tags.size()));
        at += 2;
        for (const auto& value : tags) {
            u16(names, at, unsigned(value.size()));
            u16(names, at + 2, unsigned(names.size() - storage));
            names.insert(names.end(), value.begin(), value.end());
            at += 4;
        }
    }
    Data result(44);
    font_u32(result, 0, 0x00010000);
    u16(result, 4, 2);
    font_u32(result, 12, 0x6e616d65);
    font_u32(result, 20, 44);
    font_u32(result, 24, std::uint32_t(names.size()));
    result.insert(result.end(), names.begin(), names.end());
    while (result.size() % 4)
        result.push_back(std::byte{});
    font_u32(result, 28, 0x4f532f32);
    font_u32(result, 36, std::uint32_t(result.size()));
    font_u32(result, 40, 6);
    const auto os2 = result.size();
    result.resize(os2 + 6);
    u16(result, os2 + 4, 400);
    return result;
}
Json face(const Data& bytes) {
    return forge::ui_font_detail::describe(bytes).at("font_faces").at(0);
}
Data variable(Data original, unsigned count = 1) {
    using forge::test::font_u32;
    // Extend this original synthetic two-table directory with an fvar record.
    original.insert(original.begin() + 12, 16, std::byte{});
    u16(original, 4, 3);
    for (std::size_t entry : {28u, 44u})
        font_u32(original, entry + 8, font_u32(original, entry + 8) + 16);
    const auto offset = original.size();
    const auto size = 16 + 20 * count;
    font_u32(original, 12, 0x66766172);
    font_u32(original, 20, unsigned(offset));
    font_u32(original, 24, size);
    original.resize(offset + size);
    u16(original, offset, 1);
    u16(original, offset + 4, 16);
    u16(original, offset + 6, 2);
    u16(original, offset + 8, count);
    u16(original, offset + 10, 20);
    u16(original, offset + 14, 4 + 4 * count);
    for (unsigned i = 0; i < count; ++i) {
        const auto at = offset + 16 + 20 * i;
        font_u32(original, at, i == 0 ? 0x77676874 : 0x5a303041 + i);
        font_u32(original, at + 4, 100 * 65536);
        font_u32(original, at + 8, 400 * 65536);
        font_u32(original, at + 12, 900 * 65536);
        u16(original, at + 18, 256);
    }
    return original;
}
void variations() {
    const auto valid = variable(font({{3, 1, 0x409, 256, ascii("Weight")}}));
    const auto recorded = face(valid).at("axes");
    check(recorded.size() == 1 && recorded[0] == Json{{"tag", "wght"},
                                                      {"name", "Weight"},
                                                      {"minimum", 100.},
                                                      {"default", 400.},
                                                      {"maximum", 900.},
                                                      {"hidden", false}},
          "Recorded weight axis bounds/name differ");
    const auto offset = forge::test::font_u32(valid, 20);
    const auto missing = [&](Data value) {
        forge::validate_ui_font(value);
        check(!face(value).contains("axes"), "Malformed optional axes were exposed");
    };
    for (const auto& [at, value] : {std::pair{0u, 2u},
                                    {2u, 1u},
                                    {4u, 0xffffu},
                                    {6u, 0u},
                                    {8u, 0u},
                                    {8u, 17u},
                                    {10u, 19u},
                                    {12u, 1u},
                                    {14u, 0u},
                                    {32u, 2u},
                                    {34u, 255u},
                                    {34u, 32768u}}) {
        auto invalid = valid;
        u16(invalid, offset + at, value);
        missing(std::move(invalid));
    }
    auto invalid = valid;
    forge::test::font_u32(invalid, offset + 24, 99 * 65536);
    missing(std::move(invalid));
    invalid = variable(font({}), 2);
    forge::test::font_u32(invalid, forge::test::font_u32(invalid, 20) + 36, 0x77676874);
    missing(std::move(invalid));
    auto signed_axis = valid;
    forge::test::font_u32(signed_axis, offset + 20, 0xffff8000);
    u16(signed_axis, offset + 32, 1);
    const auto signed_metadata = face(signed_axis).at("axes")[0];
    check(signed_metadata.at("minimum") == -.5 && signed_metadata.at("hidden") == true,
          "Signed fractional design coordinate/hidden flag changed");
}
void synthetic() {
    const auto named =
        font({{3, 1, 0x411, 1, ascii("Other language")},
              {3, 1, 0x409, 1, ascii("Legacy")},
              {3, 10, 0x409, 16, unicode({'C', 'a', 'f', 0xe9, ' ', 0xd83d, 0xde00})},
              {3, 1, 0x409, 2, ascii("Regular")},
              {3, 1, 0x409, 17, ascii("Book")},
              {3, 1, 0x409, 4, ascii("Full name")},
              {3, 1, 0x409, 6, ascii("PostscriptName")}});
    const auto value = face(named);
    check(value.at("family") == "Café 😀" && value.at("style") == "Book" &&
              value.at("full_name") == "Full name" &&
              value.at("postscript_name") == "PostscriptName" && value.at("weight") == 400 &&
              value.at("index") == 0,
          "Font name IDs, Unicode surrogate decoding or style weight differ");
    check(face(font({{0, 4, 0, 1, ascii("Unicode")}, {3, 1, 0x409, 1, ascii("English")}}))
                  .at("family") == "English",
          "Native-independent English selection differs");
    check(face(font({{0, 4, 0, 1, ascii("Unicode")}, {0, 4, 0x8000, 1, ascii("Tagged")}},
                    {ascii("EN-us")}))
                  .at("family") == "Tagged",
          "Version1 language tag selection failed");
    for (const auto& broken :
         {unicode({0xd800}), unicode({0xdc00}), unicode({0xd800, 'a'}), unicode({'A', 0, 'B'}),
          Data{std::byte{0}, std::byte{65}, std::byte{0}}}) {
        const auto data = font({{3, 1, 0x409, 1, broken}});
        forge::validate_ui_font(data);
        check(!face(data).contains("family"),
              "Malformed optional name became an apparently valid label");
    }
    check(face(font({{1, 0, 0, 1, ascii("Legacy ASCII", false)}})).at("family") == "Legacy ASCII",
          "Macintosh ASCII subset lost");
    check(!face(font({{1, 0, 0, 1, Data{std::byte{0x80}}}})).contains("family"),
          "Unsupported legacy encoding was guessed");
    check(face(font({{3, 1, 0x409, 1, ascii(std::string(64, 'x'))}}))
                  .at("family")
                  .get<std::string>()
                  .size() == 64,
          "Exact name byte bound rejected");
    check(!face(font({{3, 1, 0x409, 1, ascii(std::string(65, 'x'))}})).contains("family"),
          "Oversized name was truncated or retained");
    auto invalid = named;
    u16(invalid, 44 + 4, 0xffff);
    forge::validate_ui_font(invalid);
    check(!face(invalid).contains("family"), "Invalid optional string storage changed admission");
    invalid = named;
    forge::test::font_u32(invalid, 28, 0x6e616d65);
    check(!face(invalid).contains("family"), "Ambiguous naming tables supplied labels");
    const auto missing = font({});
    check(face(missing).at("weight") == 400 && !face(missing).contains("family"),
          "Nameless font lost its index/weight");
    const auto escaped = ascii(std::string(64, '\\'));
    const auto large = variable(font({{3, 1, 0x409, 1, escaped},
                                      {3, 1, 0x409, 2, escaped},
                                      {3, 1, 0x409, 4, escaped},
                                      {3, 1, 0x409, 6, escaped},
                                      {3, 1, 0x409, 256, escaped}}),
                                16);
    auto description = forge::ui_font_detail::describe(large);
    description["faces"] = 32;
    for (unsigned i = 1; i < 32; ++i) {
        auto row = description.at("font_faces")[0];
        row["index"] = i;
        row.erase("axes");
        row["axes_omitted"] = true;
        description["font_faces"].push_back(std::move(row));
    }
    forge::UiAssetSnapshot snapshot;
    snapshot.project = "/font-metadata-test";
    snapshot.documents.emplace(forge::AssetId::generate(), "Assets/Hud.rml");
    for (unsigned i = 0; i < 32; ++i)
        snapshot.sources.push_back({"Assets/" + std::to_string(i) + ".ttc", "ui_font",
                                    std::string(64, 'a'), large.size(), description});
    const auto encoded = forge::ui_inspection_detail::encode(snapshot).dump();
    const auto decoded =
        forge::ui_inspection_detail::decode(forge::asset_detail::parse_bounded_json(
            std::as_bytes(std::span(encoded)), 1024 * 1024, 100000, 16));
    check(decoded.sources == snapshot.sources,
          "Maximum escaped face labels exceeded native inspection transport bounds");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 5, "Need default, Poppins, Cairo and original variable font files");
        synthetic();
        variations();
        const auto latin = forge::asset_detail::read_bytes(argv[1], 4 * 1024 * 1024);
        const auto poppins = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        const auto cairo = forge::asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        const auto variable_font = forge::asset_detail::read_bytes(argv[4], 4 * 1024 * 1024);
        check(face(variable_font).at("axes")[0].at("name") == "Weight" &&
                  face(variable_font).at("axes")[0].at("maximum") == 900,
              "Original native variable font axis metadata differs");
        const auto many = forge::ui_font_detail::describe(forge::test::font_collection(
            {variable_font, variable_font, variable_font, variable_font, variable_font,
             variable_font, variable_font, variable_font, variable_font, variable_font,
             variable_font, variable_font, variable_font, variable_font, variable_font,
             variable_font, variable_font}));
        check(many.at("font_faces")[15].contains("axes") &&
                  many.at("font_faces")[16].at("axes_omitted") == true,
              "Collection axis metadata budget was not enforced across faces");
        check(face(latin).at("family") == "LatoLatin", "Actual default font name differs");
        check(face(poppins).at("family") == "Poppins" && face(cairo).at("family") == "Cairo",
              "Actual project font names differ");
        const auto described =
            forge::ui_font_detail::describe(forge::test::font_collection({poppins, cairo}));
        check(described.at("faces") == 2 &&
                  described.at("font_faces")[0].at("family") == "Poppins" &&
                  described.at("font_faces")[1].at("family") == "Cairo" &&
                  described.at("font_faces")[1].at("index") == 1,
              "Collection labels changed native face order");
        std::cout << "Font face labels, optional encoding/bounds and real project fonts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
