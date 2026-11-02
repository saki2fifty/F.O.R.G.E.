#pragma once
#include "../src/authored_schema.hpp"
#include <forge/runtime.hpp>

namespace forge::test {
struct VariantControls {
    float speed = 90;
    double phase = .5;
    EntityRef target{};
};
inline EngineModule variant_controls_module() {
    EngineModule module;
    module.id = "project.variant_controls";
    module.dependencies = {"forge.transforms"};
    module.schema_roles = role_mask(WorldRole::Authoring) | role_mask(WorldRole::Validation) |
                          role_mask(WorldRole::Runtime);
    module.schemas = [](ModuleContext& context) {
        const auto type = context.world.component<VariantControls>()
                              .member<float>("speed")
                              .member<double>("phase")
                              .member<EntityRef>("target");
        detail::opt_in_authoring(context.world, type, "project.variant_controls",
                                 "project.variant_controls", 1,
                                 {{"speed", 90}, {"phase", .5}, {"target", nullptr}}, "Gameplay");
    };
    return module;
}
} // namespace forge::test
