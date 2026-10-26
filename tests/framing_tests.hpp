#pragma once
#include "framing.hpp"
inline void test_framing_selection() {
    using namespace forge;
    const auto root = EntityId::generate(), child = EntityId::generate(),
               grandchild = EntityId::generate(), other = EntityId::generate();
    Json source{
        {"entities",
         Json::array({{{"id", grandchild}, {"parent", child.str()}},
                      {{"id", root}},
                      {{"id", child}, {"parent", root.str()}, {"spatial", {{"mode", "world"}}}},
                      {{"id", other}, {"spatial", {{"mode", "explicit"}, {"target", root}}}}})}};
    if (framing_entities(source, root.str()) != std::set<EntityId>{root, child, grandchild} ||
        framing_entities(source, child.str()) != std::set<EntityId>{child, grandchild} ||
        !framing_entities(source, "").empty())
        throw std::runtime_error("Framing confused structural subtree with spatial ancestry");
    source["entities"][1]["parent"] = grandchild.str();
    if (framing_entities(source, root.str()).size() != 3)
        throw std::runtime_error("Malformed framing ancestry did not terminate safely");
    const auto original = source;
    const std::vector<std::string> selected{root.str(), child.str(), "", root.str(), other.str()};
    const std::set<EntityId> expected{root, child, grandchild, other};
    if (framing_entities(source, selected) != expected)
        throw std::runtime_error("Batch framing lost overlapping, repeated or unrelated roots");
    auto reversed = source;
    auto& rows = reversed["entities"].get_ref<Json::array_t&>();
    std::reverse(rows.begin(), rows.end());
    if (framing_entities(reversed, selected) != expected || source != original)
        throw std::runtime_error("Batch framing depends on row order or changed its input");
    for (const auto& empty : std::vector<std::vector<std::string>>{{}, {"", ""}})
        if (!framing_entities(Json(), empty).empty())
            throw std::runtime_error("Empty framing selection inspected the source");
    bool invalid_rejected = false, profile_rejected = false;
    try {
        (void)framing_entities(source, std::vector<std::string>{root.str(), "invalid"});
    } catch (const std::exception&) {
        invalid_rejected = true;
    }
    try {
        const Json oversized{{"entities", Json::array_t(10001)}};
        (void)framing_entities(oversized, selected);
    } catch (const std::exception& error) {
        profile_rejected = std::string(error.what()) == "Scene framing exceeds entity profile";
    }
    if (!invalid_rejected || !profile_rejected)
        throw std::runtime_error("Batch framing bypassed identity or entity-profile admission");
    const auto camera_id = EntityId::generate(), ignored = EntityId::generate();
    auto camera_world = std::array<double, 12>{1, 0, 0, 100, 0, 1, 0, 2, 0, 0, 1, 0};
    auto ignored_world = std::array<double, 12>{1, 0, 0, 1000, 0, 1, 0, 0, 0, 0, 1, 0};
    source["entities"].push_back({{"id", camera_id}, {"world_affine", camera_world}});
    source["entities"].push_back({{"id", ignored}, {"world_affine", ignored_world}});
    RenderBounds mesh{{-1, -1, -1}, {1, 1, 1}};
    const auto combined =
        include_selected_helper_positions(source, {root, camera_id}, {root}, mesh);
    if (combined.minimum != mesh.minimum || combined.maximum != Double3{100, 2, 1})
        throw std::runtime_error(
            "Mixed mesh/helper selection omitted camera or included unselected helper");
    if (include_selected_helper_positions(source, {root, camera_id}, {root, camera_id}, mesh) !=
        mesh)
        throw std::runtime_error("Renderable helper bounds were applied twice");
    const auto region = EntityId::generate();
    source["entities"].push_back(
        {{"id", region},
         {"spatial_resolved", true},
         {"world_affine", std::array<double, 12>{2, 0, 0, 100, 0, 1, 0, 0, 0, 0, 1, 37}},
         {"components",
          {{"forge.navigation_region_2d",
            {{"enabled", true}, {"obstacle", false}, {"width", 20}, {"height", 6}}}}}});
    const auto region_bounds = include_selected_helper_positions(source, {region}, {}, mesh);
    if (region_bounds.minimum != Double3{-1, -3, -1} || region_bounds.maximum != Double3{120, 3, 1})
        throw std::runtime_error(
            "Navigation region framing omitted its full world XY footprint or used visual depth");
    if (include_selected_helper_positions(source, {region}, {region}, mesh) != region_bounds)
        throw std::runtime_error("Renderable region omitted its independent navigation footprint");
}
