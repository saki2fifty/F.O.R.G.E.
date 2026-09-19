#pragma once
#include <forge/authoring.hpp>
#include <forge/entity_recipes.hpp>
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
            require(row.at("components").at("forge.primitive").at("kind") == recipe.kind,
                    "Recipe geometry mismatch");
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
