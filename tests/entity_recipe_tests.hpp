#pragma once
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/entity_recipes.hpp>
#include <forge/render_scene.hpp>
inline void test_entity_recipes() {
    auto require = [](bool valid, const char* text) {
        if (!valid)
            throw std::runtime_error(text);
    };
    {
        forge::EngineContext engine;
        forge::Scene s(engine.world());
        for (const auto& recipe : forge::entity_recipes()) {
            const auto before = s.document();
            const auto count = s.entity_count();
            const std::string id =
                forge::authoring_command(
                    s, "entity.create",
                    {{"recipe", recipe.id}, {"position", {{"x", 2}, {"y", 3}, {"z", 4}}}})
                    .at("selected");
            const auto after = s.document();
            const auto& row = after.at("entities").back();
            require(s.entity_count() == count + 1 && row.at("id") == id,
                    "Recipe create/select mismatch");
            const auto& components = row.at("components");
            if (recipe.kind == forge::no_primitive)
                require(components.at("forge.primitive").at("kind") == recipe.kind,
                        "Empty recipe acquired legacy geometry");
            else {
                require(!components.contains("forge.primitive") &&
                            !components.contains("forge.tint"),
                        "New mesh recipe retained competing legacy geometry/shading authority");
                const auto& renderer = components.at("forge.mesh_renderer");
                require(renderer.at("mesh").get<forge::AssetId>() ==
                                forge::engine_primitive(recipe.kind).id &&
                            renderer.at("materials").size() == 1 &&
                            !renderer.at("materials")[0].at("material").is_null(),
                        "Recipe did not assign built-in mesh and material assets");
                const auto rendered = forge::extract_render_scene(s.effective_document());
                require(rendered.meshes.size() == 1 && !rendered.meshes[0].legacy_tint,
                        "New mesh recipe used legacy render projection");
            }
            require(row.at("components").at("forge.local_translation").at("y") == 3,
                    "Recipe placement ignored");
            if (!recipe.component.empty())
                require(row.at("components").contains(recipe.component),
                        "Recipe component missing");
            require(s.undo() && s.document() == before, "Recipe was not one atomic Undo step");
            require(s.redo() && s.document() == after, "Recipe Redo changed identity/composition");
            s.undo();
        }
        const auto before = s.document();
        bool rejected = false;
        try {
            forge::authoring_command(s, "entity.create", {{"recipe", "not-supported"}});
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && s.document() == before, "Unknown recipe changed scene");
    }
}
