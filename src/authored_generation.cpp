#include "authored_generation.hpp"
#include "authored_schema.hpp"
namespace forge::detail {
AuthoredGeneration::AuthoredGeneration(flecs::world& world, const nlohmann::json& copied) {
    validate_authored_types(world, copied);
    const auto adapters = authoring_value_adapters(world);
    for (const auto& declaration : copied) {
        auto type =
            std::make_unique<ReconstructedMeta>(world, declaration.at("structure"), adapters);
        const auto native = type->native_type();
        auto entity = world.entity(native);
        entity.add(flecs::OnInstantiate, flecs::Inherit);
        ecs_doc_set_name(world, native,
                         declaration.at("display_name").get_ref<const std::string&>().c_str());
        ecs_doc_set_brief(world, native,
                          declaration.at("description").get_ref<const std::string&>().c_str());
        ecs_doc_set_link(world, native,
                         declaration.at("documentation_url").get_ref<const std::string&>().c_str());
        // The candidate is private to a synchronous owning-thread publication.
        // Its metadata may coexist with the prior generation during preparation;
        // export/discovery uses only the published WorldContext codec selection.
        entity.set<AuthoredTypeAdmission>(
            {declaration.at("id"), declaration.at("module"), declaration.at("category"),
             declaration.at("schema_version"), declaration.at("defaults")});
        codecs_.push_back({native, declaration, adapters});
        types_.push_back(std::move(type));
    }
}
void AuthoredGeneration::release_to_world() noexcept {
    for (auto& type : types_)
        type->release_to_world();
}
} // namespace forge::detail
