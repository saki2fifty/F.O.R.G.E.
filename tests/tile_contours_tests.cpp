#include "cooked_envelope.hpp"
#include "tile_draw_values.hpp"
#include "tile_map_collision.hpp"
#include "tile_map_cook.hpp"
#include "tile_set_cook.hpp"
#include "tile_set_document.hpp"
#include <filesystem>
#include <iostream>

using namespace forge;
namespace {
using Json = nlohmann::json;
void require(bool condition, const char* why) {
    if (!condition)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid contour transaction was accepted");
}
Json rectangle(double x, double y, double w, double h) {
    return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
}
Json ring() {
    return {{"kind", "contours"},
            {"contours", Json::array({{{"points", rectangle(0, 0, 1, 1)},
                                       {"holes", Json::array({rectangle(.25, .25, .5, .5)})}},
                                      {{"points", rectangle(.375, .375, .25, .25)}}})}};
}
TileSetSource source() {
    auto s = TileSetSource::create(AssetId::generate());
    s.document["next_id"] = 4;
    s.document["images"] = Json::array({{{"id", 1},
                                         {"name", "Image"},
                                         {"material", AssetId::generate()},
                                         {"grid",
                                          {{"columns", 1},
                                           {"rows", 1},
                                           {"margin_x", 0},
                                           {"margin_y", 0},
                                           {"spacing_x", 0},
                                           {"spacing_y", 0}}}}});
    s.document["tiles"] =
        Json::array({{{"id", 2}, {"name", "Contours"}, {"image", 1}, {"frame", 0}},
                     {{"id", 3}, {"name", "Other"}, {"image", 1}, {"frame", 0}}});
    s.document["opaque"] = {{"keep", UINT64_MAX}};
    return s;
}
void source_and_cook() {
    auto s = source();
    const auto original = s.document;
    s.set_collision(2, ring());
    require(s.document["version"] == 6 && s.document["next_id"] == 4 &&
                s.document["asset_id"] == original["asset_id"] &&
                s.document["opaque"] == original["opaque"],
            "Contour promotion changed identity, allocator or extensions");
    auto& profile = s.document["tiles"][0]["collision"];
    profile["opaque"] = "SOURCE_ONLY_CONTOURS";
    profile["contours"][0]["label"] = "SOURCE_ONLY_CONTOURS";
    profile["contours"][1]["tag"] = UINT64_MAX;
    s.set_collision(2, ring());
    require(s.document["tiles"][0]["collision"]["contours"][0]["label"] == "SOURCE_ONLY_CONTOURS" &&
                s.document["tiles"][0]["collision"]["contours"][1]["tag"] == UINT64_MAX,
            "Typed contour edit dropped retained positional extensions");
    const auto text = s.document.dump();
    const auto parsed = TileSetSource::parse(std::as_bytes(std::span(text)));
    require(parsed.document == s.document, "Source contour roundtrip changed fields");
    const auto bytes = cook_tile_set(s);
    const auto data = decode_cooked_tile_set(bytes, s.asset(), 6);
    const auto& prepared = *data.tile(2).collision.contours;
    require(data.tile(2).collision == s.values().tiles[0].collision &&
                prepared.vertices.size() == 12 && prepared.triangles.size() == 10,
            "Cooked prepared contour geometry differs");
    const auto copy = data;
    require(copy.tile(2).collision.contours == data.tile(2).collision.contours &&
                copy.tiles().data() == data.tiles().data(),
            "Immutable geometry copy duplicated ownership");
    const std::string cooked(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(cooked.find("SOURCE_ONLY_CONTOURS") == std::string::npos &&
                cooked.find("triangles") == std::string::npos &&
                cooked.find("vertices") == std::string::npos,
            "Cooked profile retained source extensions or serialized a derived cache");
    auto plain = s;
    plain.set_collision(2, {{"kind", "none"}});
    const auto plain_data = decode_cooked_tile_set(cook_tile_set(plain), plain.asset());
    require(data.resident_bytes() >= plain_data.resident_bytes() + sizeof(TileCollisionContours) +
                                         24 * sizeof(std::array<double, 2>) +
                                         10 * sizeof(std::array<std::uint16_t, 3>),
            "Prepared contour ownership escaped resident accounting");
    auto one = ring();
    one["contours"].erase(one["contours"].begin() + 1);
    one["contours"][0].erase("holes");
    s.set_collision(2, one);
    require(s.values().tiles[0].collision.contours->contours[0].holes.empty() &&
                s.document["tiles"][0]["collision"]["contours"][0]["label"] ==
                    "SOURCE_ONLY_CONTOURS",
            "Omitted holes did not clear old loops or preserve contour siblings");
    s.set_collision(2, {{"kind", "polygon"}, {"points", rectangle(0, 0, 1, 1)}});
    require(!s.document["tiles"][0]["collision"].contains("contours") && s.document["version"] == 6,
            "Kind change retained unused contours or downgraded source");
    const auto envelope = asset_detail::decode_envelope(
        bytes, tile_set_cooked_magic, tile_set_cooked_metadata_limit, 0, tile_set_cooked_version);
    for (const char* field : {"opaque", "points"}) {
        auto metadata = envelope.metadata;
        metadata["tiles"][0]["collision"][field] = 42;
        reject([&] {
            (void)decode_cooked_tile_set(
                asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                              tile_set_cooked_metadata_limit, 6),
                s.asset());
        });
    }
    auto metadata = envelope.metadata;
    metadata["tiles"][0]["collision"]["contours"][0]["label"] = "reject";
    reject([&] {
        (void)decode_cooked_tile_set(
            asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                          tile_set_cooked_metadata_limit, 6),
            s.asset());
    });
    reject([&] {
        (void)decode_cooked_tile_set(
            asset_detail::encode_envelope(envelope.metadata, {}, tile_set_cooked_magic,
                                          tile_set_cooked_metadata_limit, 5),
            s.asset());
    });
    for (unsigned version = 1; version <= 5; ++version) {
        auto old = source();
        old.document["version"] = version;
        old.document["tiles"][1]["collision"] = {{"kind", "full_cell"},
                                                 {"contours", {{"legacy", true}}}};
        const auto unchanged = old.document;
        require(old.values().tiles[1].collision.kind == TileCollisionKind::FullCell &&
                    !old.values().tiles[1].collision.contours,
                "Old opaque contours acquired geometry");
        reject([&] { old.set_collision(2, ring()); });
        require(old.document == unchanged, "Rejected all-row promotion changed source");
        if (version >= 2) {
            old.set_collision(2, {{"kind", "none"}});
            require(old.document["version"] == version &&
                        old.document["tiles"][1]["collision"]["contours"] ==
                            unchanged["tiles"][1]["collision"]["contours"],
                    "Old typed collision edit damaged opaque contours");
        }
    }
}
void history(const std::filesystem::path& root) {
    auto lease = std::make_shared<ProjectLease>(root);
    auto doc = TileSetDocument::create(lease, "Contours.tileset.json");
    doc->edit(doc->revision(), "Definitions", [&](auto& j) {
        const auto id = j["asset_id"];
        j = source().document;
        j["asset_id"] = id;
    });
    const auto old = doc->source().document;
    const auto revision = doc->revision();
    doc->edit(revision, "Tile contours", [](auto& j) {
        TileSetSource candidate{j};
        candidate.set_collision(2, ring());
        j = std::move(candidate.document);
    });
    const auto admitted = doc->source().document;
    reject([&] { doc->edit(revision, "Stale contours", [](auto&) {}); });
    auto invalid = ring();
    invalid["contours"][0]["holes"][0] = rectangle(0, .25, .5, .5);
    reject([&] {
        doc->edit(doc->revision(), "Touching hole", [&](auto& j) {
            TileSetSource candidate{j};
            candidate.set_collision(2, invalid);
            j = std::move(candidate.document);
        });
    });
    require(doc->source().document == admitted, "Invalid edit changed admitted document");
    doc->undo();
    require(doc->source().document == old, "Undo did not restore previous source");
    doc->redo();
    require(doc->source().document == admitted, "Redo changed contour source");
    doc->save();
    TileSetDocument reopened(lease, "Contours.tileset.json");
    require(reopened.source().document == admitted, "Saved contours changed on reopen");
}
bool covers(const TileMapCollisionGeometry& geometry, std::array<double, 2> p) {
    for (const auto& piece : geometry.polygons) {
        bool positive = true, negative = true;
        for (std::size_t i = 0; i < piece.count; ++i) {
            const auto a = piece.points[i], b = piece.points[(i + 1) % piece.count];
            const long double turn = (static_cast<long double>(b[0]) - a[0]) * (p[1] - a[1]) -
                                     (static_cast<long double>(b[1]) - a[1]) * (p[0] - a[0]);
            positive &= turn >= 0;
            negative &= turn <= 0;
        }
        if (positive || negative)
            return true;
    }
    return false;
}
void projection() {
    auto s = source();
    s.set_collision(2, ring());
    // Off-center hole and island make orientation errors observable.
    auto shape = ring();
    shape["contours"][0]["holes"][0] = rectangle(.125, .25, .5, .5);
    shape["contours"][1]["points"] = rectangle(.25, .375, .125, .125);
    s.set_collision(2, shape);
    s.set_autotile(3, {{"enabled", true}, {"rules", {{{"mask", 0}, {"same", 0}, {"tile", 2}}}}});
    auto definitions = decode_cooked_tile_set(cook_tile_set(s), s.asset());
    auto map = TileMapValue::create(AssetId::generate(), s.asset());
    auto metadata = map.metadata();
    metadata["layers"] =
        Json::array({{{"id", 1}, {"name", "Solid"}, {"visible", true}, {"order", 0}}});
    metadata["next_id"] = 2;
    map = map.with_metadata(metadata);
    for (unsigned flags = 0; flags < 8; ++flags)
        for (std::uint64_t logical : {2u, 3u}) {
            const TileMapCellEdit edit{1, {-33, -1}, {logical, flags}};
            auto cells = map.paint(std::span(&edit, 1));
            auto cooked = decode_cooked_tile_map(cook_tile_map(cells), cells.asset());
            const auto geometry = tile_map_collision_geometry(cooked, definitions, 1, 10);
            require(geometry.rectangles.empty() && geometry.polygons.size() == 10 &&
                        cooked.cell(1, {-33, -1}).tile == logical,
                    "Contours lost pieces or changed logical cell");
            const auto draw = tile_draw_values(definitions, 2, flags, 16, 16);
            for (unsigned y = 0; y < 23; ++y)
                for (unsigned x = 0; x < 23; ++x) {
                    const std::array<double, 2> p{(x + .31) / 23, (y + .61) / 23};
                    auto uv = std::array{p[0], 1 - p[1]};
                    if (draw.sprite.transpose_uv)
                        std::swap(uv[0], uv[1]);
                    for (unsigned axis = 0; axis < 2; ++axis)
                        uv[axis] = draw.sprite.region.origin[axis] +
                                   uv[axis] * draw.sprite.region.scale[axis];
                    const double sx = uv[0], sy = 1 - uv[1];
                    const bool hole = sx > .125 && sx < .625 && sy > .25 && sy < .75;
                    const bool island = sx > .25 && sx < .375 && sy > .375 && sy < .5;
                    require(covers(geometry, p) == (!hole || island),
                            "Collision coverage differs from drawn orientation");
                }
            reject([&] { (void)tile_map_collision_geometry(cooked, definitions, 1, 9); });
            require(tile_map_collision_geometry(cooked, definitions, 1) == geometry,
                    "Rejected capacity candidate changed subsequent geometry");
            for (const auto& piece : geometry.polygons)
                require(piece.origin == TileCellAddress{-33, -1},
                        "Contour piece lost negative seam address");
        }
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected dedicated contour test directory");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        source_and_cook();
        history(root);
        projection();
        std::filesystem::remove_all(root);
        std::cout << "Contour source/cooked6, old opaque guards, extensions, IDs/history, "
                     "immutable bytes and16 direct/neighbor-selected oriented collision "
                     "projections PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
