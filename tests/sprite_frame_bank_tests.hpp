#pragma once
#include "reconstructed_meta.hpp"
#include "render_values.hpp"
#include <forge/authoring.hpp>

inline void test_sprite_frame_banks(const std::filesystem::path& root) {
    using namespace forge;
    const auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Frame bank"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.sprite_renderer"}});
    Json frames = Json::array({{{"x", 0}, {"y", 0}, {"width", 4}, {"height", 3}},
                               {{"x", 8}, {"y", 1}, {"width", 7}, {"height", 8}},
                               {{"x", 2}, {"y", 10}, {"width", 3}, {"height", 2}}});
    const auto before = scene.document();
    authoring_command(scene, "property.set_many",
                      {{"entity", actor},
                       {"component", "forge.sprite_renderer"},
                       {"values", {{"frame_regions", frames}, {"frame", 2}}}});
    const auto authored = scene.document();
    require(scene.entity(actor).get<SpriteRenderer>().frame_regions.size() == 3 && scene.undo() &&
                scene.document() == before && scene.redo() && scene.document() == authored,
            "Frame bank authoring did not preserve typed values and atomic history");
    std::vector<Json> invalid_banks{Json::array({frames[0]})};
    for (const auto& [field, value] : std::vector<std::pair<std::string, Json>>{
             {"x", -1}, {"y", .5}, {"width", 0}, {"height", 16385}, {"x", 4294967296ULL}}) {
        auto invalid = frames;
        invalid[0][field] = value;
        invalid_banks.push_back(std::move(invalid));
    }
    for (const auto& invalid : invalid_banks) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "property.set",
                              {{"entity", actor},
                               {"component", "forge.sprite_renderer"},
                               {"field", "frame_regions"},
                               {"value", invalid}});
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && scene.document() == authored && scene.revision() == revision,
                "Invalid bank partially changed Scene, revision or history");
    }
    auto sliced_frames = frames;
    sliced_frames[0]["width"] = 2; // Only this unselected frame loses its source center.
    bool rejected_border = false;
    try {
        authoring_command(scene, "property.set_many",
                          {{"entity", actor},
                           {"component", "forge.sprite_renderer"},
                           {"values",
                            {{"image_mode", 1},
                             {"border_left", 1},
                             {"border_right", 1},
                             {"frame_regions", sliced_frames}}}});
    } catch (const std::exception&) {
        rejected_border = true;
    }
    require(rejected_border && scene.document() == authored,
            "Sliced admission ignored an invalid unselected frame's borders");
    Json full = Json::array();
    for (std::size_t i = 0; i < sprite_frame_region_limit; ++i)
        full.push_back(frames.front());
    authoring_command(scene, "property.set_many",
                      {{"entity", actor},
                       {"component", "forge.sprite_renderer"},
                       {"values", {{"frame_regions", full}, {"frame", full.size() - 1}}}});
    require(scene.entity(actor).get<SpriteRenderer>().frame_regions.size() ==
                    sprite_frame_region_limit &&
                scene.undo() && scene.document() == authored,
            "The advertised frame-bank ceiling did not admit through the shared value envelope");
    const auto world = engine.world().world();
    const detail::ReflectedAdapter adapters[] = {
        {world.id<AssetRef<MaterialAsset>>(), "asset_ref", MaterialAsset::type},
        {world.id<std::vector<SpriteFrameRegion>>(), "vector"}};
    const auto native = detail::reflected_type_schema(world, world.id<SpriteRenderer>(), adapters);
    Json fields = Json::array();
    for (const auto& field : native.at("fields"))
        if (field.at("id") == "frame_regions")
            fields.push_back(field);
    require(fields.size() == 1 && fields[0].at("maximum_count") == sprite_frame_region_limit,
            "Frame-bank discovery lost the native semantic count limit");
    const Json projection{{"type", "struct"}, {"fields", fields}};
    flecs::world copied;
    const detail::ReconstructedMeta metadata(copied, projection);
    const Json values{{"frame_regions", frames}};
    const detail::ReflectedCandidate candidate(copied, metadata.native_type(), values);
    require(detail::read_reflected_native(copied, metadata.native_type(), candidate.data()) ==
                values,
            "Copied frame-bank metadata failed native value round-trip");
    for (const Json count : {Json(0), Json(-1), Json(4097), Json(1.5)}) {
        auto invalid = projection;
        invalid["fields"][0]["maximum_count"] = count;
        bool rejected = false;
        try {
            const detail::ReconstructedMeta bad(copied, invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Malformed copied collection limit was admitted");
    }
    const auto file = root / "FrameBank.scene.json";
    scene.save(file);
    Scene reopened(engine.world());
    reopened.load(file);
    require(reopened.document() == authored &&
                reopened.entity(actor).get<SpriteRenderer>().frame_regions.size() == 3,
            "Frame bank did not reopen exactly");
}
