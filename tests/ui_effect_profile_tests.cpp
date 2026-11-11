#include "ui_material_effect.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid native UI Material effect admitted");
}
} // namespace
int main() {
    try {
        const auto material = forge::AssetId::generate();
        const Rml::Dictionary effect{{"value", Rml::Variant("material:" + material.str())},
                                     {"dimensions", Rml::Variant(Rml::Vector2f{160, 80})}};
        const auto copied = forge::ui_effect_detail::admit_material_effect(effect);
        check(copied.material.id == material && copied.dimensions == std::array{160.f, 80.f},
              "UI effect did not retain its typed identity and copied dimensions");
        for (const char* key : {"value", "dimensions", "extra"}) {
            auto invalid = effect;
            invalid[key] = Rml::Variant(false);
            reject([&] { forge::ui_effect_detail::admit_material_effect(invalid); });
            invalid = effect;
            invalid.erase(key);
            if (std::string_view(key) != "extra")
                reject([&] { forge::ui_effect_detail::admit_material_effect(invalid); });
        }
        for (const auto* value :
             {"shader-name", "material:bad", "material:00000000-0000-0000-0000-000000000000"}) {
            auto invalid = effect;
            invalid["value"] = Rml::Variant(Rml::String(value));
            reject([&] { forge::ui_effect_detail::admit_material_effect(invalid); });
        }
        for (const auto size : {-1.f, 65537.f, std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()})
            for (const bool y : {false, true}) {
                auto invalid = effect;
                invalid["dimensions"] = Rml::Variant(Rml::Vector2f{y ? 1.f : size, y ? size : 1.f});
                reject([&] { forge::ui_effect_detail::admit_material_effect(invalid); });
            }
        std::cout
            << "Native UI Material identity, exact fields/types and finite dimensions passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
