#pragma once
#include <memory>
#include <string>
namespace Rml {
class FontEngineInterface;
}
namespace forge {
// Installed only by the owning presenter before RmlUi initialization.
std::unique_ptr<Rml::FontEngineInterface> make_ui_shaped_font(std::string& diagnostic);
} // namespace forge
