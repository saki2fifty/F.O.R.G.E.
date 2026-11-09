#pragma once
#include "ui_source_automation.hpp"
#include "view_image.hpp"

namespace forge::ui {
// Borrow the observed source image until the existing image task submits its
// native copy. The presenter and source widget retain their own lifetimes.
inline ViewImageTask::RenderedInput
ui_source_image_input(UiSourceEditor& editor, SceneDocument& project,
                      std::function<Diligent::ITextureView*()> image,
                      const Json& observed = Json()) {
    ViewImageTask::RenderedInput input;
    input.source = observed.is_null() ? editor.image_source(project)
                                      : UiSourceAutomation::image_source(editor, project, observed);
    input.current = [&editor, &project, image = std::move(image), expected = input.source] {
        if (editor.image_source(project) != expected)
            throw AuthoringError("stale_ui_preview",
                                 "UI source preview changed before image capture");
        return image();
    };
    return input;
}
} // namespace forge::ui
