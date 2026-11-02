#include "navigation_collision_collection.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F&& call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed collision selection was accepted");
}
} // namespace
int main() {
    try {
        using namespace forge;
        using navigation_detail::collect_static_collider;
        NavigationSettings settings;
        Json legacy = settings;
        require(legacy.size() == 6 && !legacy.contains("collect_static_colliders") &&
                    !legacy.contains("collision_layers"),
                "Legacy settings representation changed");
        const auto old = legacy.get<NavigationSettings>();
        require(old == settings, "Legacy settings defaults changed");
        settings.collect_static_colliders = true;
        settings.collision_layers = 0x80000001u;
        Json configured = settings;
        require(configured.get<NavigationSettings>() == settings,
                "Source selection did not round-trip exactly");
        for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
            settings.plane = plane;
            const char* key =
                plane == NavigationPlane::PlanarXY ? "forge.physics_body_2d" : "forge.physics_body";
            const char* other =
                plane == NavigationPlane::PlanarXY ? "forge.physics_body" : "forge.physics_body_2d";
            const char* marker = plane == NavigationPlane::PlanarXY ? "forge.navigation_region_2d"
                                                                    : "forge.navigation_surface";
            Json components = {
                {key, {{"enabled", true}, {"sensor", false}, {"motion", 0}, {"layer", 31}}}};
            require(collect_static_collider(components, settings), "Layer31 was lost");
            require(!collect_static_collider(Json{{other, components.at(key)}}, settings),
                    "An unrelated physics plane was included");
            components[key]["layer"] = 0;
            require(collect_static_collider(components, settings), "Layer0 was lost");
            components[key]["layer"] = 1;
            require(!collect_static_collider(components, settings), "Masked layer was included");
            components[key]["layer"] = 31;
            for (unsigned motion : {1u, 2u}) {
                components[key]["motion"] = motion;
                require(!collect_static_collider(components, settings),
                        "A moving body became static bake geometry");
            }
            components[key]["motion"] = 0;
            components[key]["sensor"] = true;
            require(!collect_static_collider(components, settings), "A sensor became solid");
            components[key]["sensor"] = false;
            components[key]["enabled"] = false;
            require(!collect_static_collider(components, settings), "Disabled body was included");
            components[key]["enabled"] = true;
            for (bool enabled : {false, true}) {
                components[marker] = {{"enabled", enabled}};
                require(!collect_static_collider(components, settings),
                        "Explicit navigation marker did not take precedence");
            }
            components.erase(marker);
            for (const Json& motion : {Json(-1), Json(3), Json(0.5), Json("0")}) {
                components[key]["motion"] = motion;
                rejects([&] { (void)collect_static_collider(components, settings); });
            }
            components[key]["motion"] = 0;
            for (const Json& layer : {Json(-1), Json(32), Json(1.5), Json("31")}) {
                components[key]["layer"] = layer;
                rejects([&] { (void)collect_static_collider(components, settings); });
            }
        }
        for (const Json& mask : {Json(-1), Json(std::uint64_t(1) << 32), Json(0.5), Json("1")}) {
            auto invalid = configured;
            invalid["collision_layers"] = mask;
            rejects([&] { (void)invalid.get<NavigationSettings>(); });
        }
        for (const Json& flag : {Json(0), Json(1), Json("true"), Json(nullptr)}) {
            auto invalid = configured;
            invalid["collect_static_colliders"] = flag;
            rejects([&] { (void)invalid.get<NavigationSettings>(); });
        }
        std::cout << "Navigation collision selection and legacy settings pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
