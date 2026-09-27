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
}
