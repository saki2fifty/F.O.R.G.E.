#pragma once
#include <RmlUi/Core/TextShapingContext.h>
#include <cstdint>
#include <string_view>
#include <vector>

struct hb_font_t;
namespace forge {
// Transient font-engine values. Source text and native layout retain ownership.
struct UiShapedGlyph {
    std::uint32_t index = 0, cluster_byte = 0;
    float x = 0, y = 0;
};
struct UiShapedRun {
    std::vector<UiShapedGlyph> glyphs;
    float advance = 0;
    bool right_to_left = false;
};
// Borrows a font scaled to 26.6 pixel units. No file, DOM, GPU or project writes.
UiShapedRun shape_ui_font_run(hb_font_t*, std::string_view, const Rml::TextShapingContext&);
} // namespace forge
