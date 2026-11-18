#include "navigation_area_value.hpp"
#include <forge/authoring.hpp>
#include <iostream>
#include <limits>
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
    throw std::runtime_error("Invalid navigation area data was accepted");
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto id = authoring_command(scene, "entity.create", {{"name", "Area policy"}})
                            .at("selected")
                            .get<std::string>();
        for (const auto* component : {"forge.navigation_area", "forge.navigation_area_filter"})
            authoring_command(scene, "component.add", {{"entity", id}, {"component", component}});
        check(scene.entity(id).get<NavigationArea>() == NavigationArea{} &&
                  scene.entity(id).get<NavigationAreaFilter>() == NavigationAreaFilter{},
              "Add Component lost typed navigation area defaults");
        auto native = engine.world().world().entity().add<NavigationAreaFilter>();
        check(native.get<NavigationAreaFilter>() == NavigationAreaFilter{},
              "Native array Meta replaced the C++ default constructor");
        native.destruct();
        bool choices = false, array = false;
        const auto schema = scene.schema();
        for (const auto& component : schema.at("components")) {
            const auto name = component.at("id").get<std::string>();
            if (name != "forge.navigation_area" && name != "forge.navigation_area_filter")
                continue;
            for (const auto& field : component.at("fields")) {
                check(!field.at("animatable").get<bool>(),
                      "Navigation policy was incorrectly advertised as animatable");
                const auto key = field.at("id").get<std::string>();
                if (name == "forge.navigation_area" && key == "area")
                    choices = field.at("choices").size() == navigation_area_limit &&
                              field.at("choices").back().at("value").get<unsigned>() == 15;
                if (key == "costs")
                    array = field.at("type").get<std::string>() == "array" &&
                            field.at("count").get<unsigned>() == 16 &&
                            field.at("default").size() == 16 &&
                            field.at("element").at("type").get<std::string>() == "float32" &&
                            field.at("minimum").get<float>() == 1 &&
                            field.at("maximum").get<float>() == navigation_area_max_cost;
            }
        }
        check(choices && array, "Inspector/automation discovery lost area/fixed-array metadata");
        const auto set = [&](const char* component, const char* field, Json value) {
            authoring_command(scene, "property.set",
                              {{"entity", id},
                               {"component", component},
                               {"field", field},
                               {"value", std::move(value)}});
        };
        set("forge.navigation_area", "area", 15);
        set("forge.navigation_area_filter", "mask", 4);
        auto costs = navigation_detail::area_filter_value(NavigationAreaFilter{}).at("costs");
        costs[2] = 8.f;
        const auto before_costs = scene.document();
        set("forge.navigation_area_filter", "costs", costs);
        const auto edited = scene.document();
        check(scene.entity(id).get<NavigationAreaFilter>().mask == 4 &&
                  scene.entity(id).get<NavigationAreaFilter>().costs[2] == 8 && scene.undo() &&
                  scene.document() == before_costs && scene.redo() && scene.document() == edited,
              "Fixed-array property changes lost typed storage or ordinary history");
        const auto expect_rejection = [&](const char* component, const char* field, Json value) {
            const auto before = scene.document();
            rejects([&] { set(component, field, std::move(value)); });
            check(scene.document() == before, "Invalid policy partially changed the Scene");
        };
        for (const auto& invalid : {Json(-1), Json(16), Json(.5), Json(true), Json("1")})
            expect_rejection("forge.navigation_area", "area", invalid);
        for (const auto& invalid : {Json(-1), Json(65536), Json(.5), Json(true)})
            expect_rejection("forge.navigation_area_filter", "mask", invalid);
        for (const auto& invalid : {Json(0), Json(.99), Json(1000001), Json(true),
                                    Json(std::numeric_limits<double>::quiet_NaN()),
                                    Json(std::numeric_limits<double>::infinity())}) {
            auto damaged = costs;
            damaged[2] = invalid;
            expect_rejection("forge.navigation_area_filter", "costs", damaged);
        }
        auto short_array = costs;
        short_array.erase(short_array.end() - 1);
        expect_rejection("forge.navigation_area_filter", "costs", short_array);
        auto long_array = costs;
        long_array.push_back(1);
        expect_rejection("forge.navigation_area_filter", "costs", long_array);
        set("forge.navigation_area_filter", "mask", 0);
        check(scene.entity(id).get<NavigationAreaFilter>().mask == 0,
              "An empty allowed-area mask was silently replaced");
        const auto snapshot = scene.snapshot();
        EngineContext restored;
        Scene copy(restored.world());
        copy.restore_snapshot(snapshot);
        check(copy.entity(id).get<NavigationArea>() == scene.entity(id).get<NavigationArea>() &&
                  copy.entity(id).get<NavigationAreaFilter>() ==
                      scene.entity(id).get<NavigationAreaFilter>(),
              "Area policy values did not survive fresh World reconstruction");
        NavigationAreaFilter invalid;
        invalid.mask = 65536;
        rejects([&] { navigation_detail::validate_navigation_area_filter(invalid); });
        invalid = {};
        invalid.costs[3] = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { navigation_detail::validate_navigation_area_filter(invalid); });
        std::cout << "Area defaults/Meta/atomic refusal/history/fresh World values passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
