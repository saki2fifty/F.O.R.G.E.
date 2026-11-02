#include "navigation_link_value.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid authored navigation link was accepted");
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto id = authoring_command(scene, "entity.create", {{"name", "Island connection"}})
                            .at("selected")
                            .get<std::string>();
        authoring_command(scene, "component.add",
                          {{"entity", id}, {"component", "forge.navigation_link"}});
        const auto initial = scene.entity(id).get<NavigationLink>();
        check(initial == NavigationLink{}, "Ordinary Add Component did not use link defaults");
        const auto schema = scene.schema();
        bool plane_choices = false;
        for (const auto& component : schema.at("components"))
            if (component.at("id") == "forge.navigation_link")
                for (const auto& field : component.at("fields")) {
                    check(!field.at("animatable").get<bool>(),
                          "Static authored link input was advertised as animatable");
                    if (field.at("id") == "plane") {
                        const auto& choices = field.at("choices");
                        plane_choices = choices.size() == 2 && choices[0].at("value") == 0 &&
                                        choices[0].at("label") == "3D" &&
                                        choices[1].at("value") == 1 &&
                                        choices[1].at("label") == "2D (XY)" &&
                                        !field.at("animatable").get<bool>();
                    }
                }
        check(plane_choices,
              "Inspector/automation discovery lost the typed navigation plane choices");
        const auto set = [&](const char* field, Json value) {
            authoring_command(scene, "property.set",
                              {{"entity", id},
                               {"component", "forge.navigation_link"},
                               {"field", field},
                               {"value", value}});
        };
        set("profile", "Large");
        set("plane", 1);
        const auto saved = scene.document();
        for (const auto& [field, value] :
             std::initializer_list<std::pair<const char*, Json>>{{"profile", "not a profile"},
                                                                 {"profile", "1wrong"},
                                                                 {"profile", ""},
                                                                 {"profile", std::string(65, 'a')},
                                                                 {"profile", 7},
                                                                 {"plane", 2},
                                                                 {"plane", -1},
                                                                 {"radius", 0},
                                                                 {"radius", 6},
                                                                 {"radius", true},
                                                                 {"end_x", -1},
                                                                 {"start_z", 1}}) {
            rejects([&] { set(field, value); });
            check(scene.document() == saved, "Rejected link property edit changed the Scene");
        }
        check(scene.undo() && scene.entity(id).get<NavigationLink>().plane == 0,
              "Link plane did not Undo through ordinary Scene history");
        check(scene.redo() && scene.document() == saved, "Link property Redo changed saved values");
        EngineContext copied_engine;
        Scene copied(copied_engine.world());
        copied.restore_snapshot(saved);
        auto value = copied.entity(id).get<NavigationLink>();
        check(value.profile == "Large" && value.plane == 1 &&
                  navigation_detail::link_from_value(navigation_detail::link_value(value)) == value,
              "Link profile, basis or source value did not survive ordinary Scene restoration");
        value.profile = "Small";
        copied.entity(id).set<NavigationLink>(value);
        check(scene.entity(id).get<NavigationLink>().profile == "Large",
              "Copied native profile string aliases the original Scene");
        const auto prefab = create_prefab_source(scene, id);
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto instance = instantiate_prefab(scene, prefab.asset());
        check(scene.entity(instance).get<NavigationLink>() ==
                      scene.entity(id).get<NavigationLink>() &&
                  !scene.entity(instance).owns<NavigationLink>(),
              "Prefab link did not inherit its native profile and endpoints");
        authoring_command(scene, "property.set",
                          {{"entity", instance},
                           {"component", "forge.navigation_link"},
                           {"field", "profile"},
                           {"value", "PrefabRoute"}});
        const auto overridden = scene.document();
        check(scene.undo() && scene.redo() && scene.document() == overridden &&
                  scene.entity(instance).get<NavigationLink>().profile == "PrefabRoute" &&
                  scene.entity(id).get<NavigationLink>().profile == "Large",
              "Prefab link string override/history changed the source or lost ownership");
        authoring_command(
            scene, "property.revert",
            {{"entity", instance}, {"component", "forge.navigation_link"}, {"field", "profile"}});
        check(scene.entity(instance).get<NavigationLink>().profile == "Large",
              "Prefab link property Revert did not resume the source profile");
        const auto duplicate = authoring_command(scene, "entity.duplicate", {{"entity", instance}})
                                   .at("selected")
                                   .get<std::string>();
        check(scene.reference(instance).entity != scene.reference(duplicate).entity &&
                  scene.entity(duplicate).get<NavigationLink>() ==
                      scene.entity(instance).get<NavigationLink>(),
              "Duplicating a prefab link lost its values or reused source entity identity");
        std::cout << "Authored link defaults/properties/rollback/history/Scene/string ownership "
                     "and prefab inheritance/override/revert/duplicate PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
