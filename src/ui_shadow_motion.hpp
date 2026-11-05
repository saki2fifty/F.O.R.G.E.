#pragma once
#include <array>
#include <memory>
#include <string_view>
namespace Rml {
class Context;
class ElementInstancer;
} // namespace Rml
namespace forge::ui_shadow_detail {
inline constexpr std::string_view tag = "forge-shadow-panel";
inline constexpr std::array<std::string_view, 6> properties{
    "forge-shadow-x",      "forge-shadow-y",     "forge-shadow-blur",
    "forge-shadow-spread", "forge-shadow-color", "forge-shadow-inset"};
} // namespace forge::ui_shadow_detail
namespace forge {
std::unique_ptr<Rml::ElementInstancer> register_ui_shadow_motion();
// Existing context update owns interpolation and time. Refresh only derived
// shadow properties before preparation/rendering, without updating context twice.
void synchronize_ui_shadow_motion(Rml::Context&);
} // namespace forge
