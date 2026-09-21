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
}
