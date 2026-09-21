#pragma once
#include <forge/primitive_catalog.hpp>
#include <string>
#include <vector>
namespace forge {
// Descriptions of ECS compositions, not entity classes. Component defaults remain schema-owned.
struct EntityRecipe {
    std::string id, label, category;
    unsigned kind;
    std::string component;
};
inline const std::vector<EntityRecipe>& entity_recipes() {
    static const auto recipes = [] {
        std::vector<EntityRecipe> out{{"empty", "Empty Entity", "", no_primitive, {}}};
        for (unsigned kind = 0; kind < primitive_count; ++kind)
            if (kind != no_primitive)
                out.push_back({"primitive." + std::to_string(kind),
                               primitive_names[kind],
                               "3D Primitive",
                               kind,
                               {}});
        for (auto recipe :
             {EntityRecipe{"render.camera", "Camera", "Rendering", no_primitive, "forge.camera"},
              {"render.light", "Light", "Rendering", no_primitive, "forge.light"},
              {"render.mesh", "Mesh Renderer", "Rendering", no_primitive, "forge.mesh_renderer"},
              {"audio.source", "Audio Source", "Audio", no_primitive, "forge.audio_source"},
              {"audio.listener", "Audio Listener", "Audio", no_primitive, "forge.audio_listener"},
              {"navigation.surface", "Navigation Surface", "Navigation", 3,
               "forge.navigation_surface"},
              {"navigation.agent", "Navigation Agent", "Navigation", no_primitive,
               "forge.navigation_agent"},
              {"ui.document", "UI Document", "UI", no_primitive, "forge.ui_document"}})
            out.push_back(recipe);
        return out;
    }();
    return recipes;
}
inline const EntityRecipe* entity_recipe(const std::string& id) {
    for (const auto& r : entity_recipes())
        if (r.id == id)
            return &r;
    return nullptr;
}
} // namespace forge
