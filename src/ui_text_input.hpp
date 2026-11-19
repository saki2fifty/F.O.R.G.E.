#pragma once
#include <memory>
#include <string>

namespace Rml {
class Context;
class TextInputHandler;
} // namespace Rml
namespace forge {
// Observes the public IME context and prepares the existing native field's selection.
// Field values, cursor actions, restrictions and events remain owned by RmlUi.
class UiTextInput {
  public:
    UiTextInput(Rml::TextInputHandler* delegate, std::string& diagnostic);
    ~UiTextInput();
    Rml::TextInputHandler* handler();
    bool key_down(Rml::Context&, int key, int modifiers);
    bool mouse_move(Rml::Context&, int x, int y, int modifiers, bool primary_down);
    bool mouse_button(Rml::Context&, int button, bool down, int modifiers);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace forge
