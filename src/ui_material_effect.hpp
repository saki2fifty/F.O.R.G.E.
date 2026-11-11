#pragma once
#include "ui_effect_profile.hpp"
#include <RmlUi/Core/Variant.h>

namespace forge::ui_effect_detail {
// Native decorator parameters are copied once. This reader has no source parser,
// catalog, compiler or graphics authority.
MaterialEffect admit_material_effect(const Rml::Dictionary&);
} // namespace forge::ui_effect_detail
