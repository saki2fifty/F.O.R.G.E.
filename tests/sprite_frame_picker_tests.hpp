#pragma once
#include "editor/sprite_frame_picker.hpp"
#include <filesystem>
#include <stdexcept>

namespace forge::test_sprite_picker {
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline void exercise(float scale, ImVec2 display = {1920, 1200}) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    check(std::filesystem::is_regular_file(font), "Missing actual UI font");
    io.FontDefault = io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::SpriteFramePicker picker;
    std::string owner = "scene/entity-one";
    ui::SpriteFrameContext context{owner, {}, {4, 1}};
    std::uint32_t value = 0, changes = 0, retries = 0;
    unsigned image_width = 64, image_height = 16;
    ResourceIdentity revision;
    revision.slot = revision.generation = 1;
    revision.type = "texture";
    revision.revision = std::string(64, 'a');
    bool ready = true, draw_field = true, disabled = false;
    std::array<float, 4> crop{};
    static int texture_marker;
    const auto image = reinterpret_cast<ImTextureID>(&texture_marker);
    picker.image_source = [&](auto) {
        ui::SpriteSheetImage result;
        if (!ready) {
            result.status = "Image not ready";
            result.retry = [&] { ++retries; };
            return result;
        }
        result.width = image_width;
        result.height = image_height;
        result.revision = revision;
        result.render = [&](unsigned w, unsigned h, std::array<float, 4> region) {
            check(w > 0 && h > 0 && w <= 2048 && h <= 2048, "Picker exceeded preview bounds");
            check(region[0] >= 0 && region[1] >= 0 && region[2] <= 1 && region[3] <= 1 &&
                      region[0] < region[2] && region[1] < region[3],
                  "Picker requested an invalid image crop");
            crop = region;
            return image;
        };
        return result;
    };
    test::observe_ui = true;
    const auto frame = [&] {
        test::ui_targets.clear();
        picker.begin_frame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize(
            {std::min(1400.f, display.x - 40), std::min(1000.f, display.y - 40)});
        ImGui::Begin("Sprite field input");
        if (draw_field) {
            ImGui::PushID("sprite-frame");
            context.owner = owner;
            ImGui::BeginDisabled(disabled);
            changes += picker.draw(context, value);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::End();
        picker.after_ui();
        ImGui::Render();
    };
    const auto box = [&](const std::string& id) {
        const auto found = test::ui_targets.find(id);
        check(found != test::ui_targets.end(), "Missing frame-picker widget");
        const auto result = found->second;
        check(result.minimum.x >= result.clip_minimum.x &&
                  result.maximum.x <= result.clip_maximum.x &&
                  result.minimum.y >= result.clip_minimum.y &&
                  result.maximum.y <= result.clip_maximum.y,
              "Frame-picker control was clipped");
        return result;
    };
    const auto press_at = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    const auto click = [&](const std::string& id) {
        frame();
        const auto target = box(id);
        check(target.enabled, "Frame-picker control was disabled");
        press_at(
            {(target.minimum.x + target.maximum.x) / 2, (target.minimum.y + target.maximum.y) / 2});
    };
    const auto key = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        frame();
    };
    for (unsigned i = 0; i < 4; ++i)
        frame();
    click("sprite:choose-frame");
    auto canvas = box("sprite:sheet-canvas");
    press_at({canvas.minimum.x + (canvas.maximum.x - canvas.minimum.x) * .875f,
              (canvas.minimum.y + canvas.maximum.y) / 2});
    check(value == 3 && changes == 1 && !picker.open(),
          "Image click did not choose exactly one row-major frame");
    click("sprite:choose-frame");
    key(ImGuiKey_LeftArrow);
    check(value == 3 && changes == 1,
          "Keyboard preview changed a caller value before confirmation");
    key(ImGuiKey_Enter);
    check(value == 2 && changes == 2 && !picker.open(), "Keyboard frame confirmation failed");
    click("sprite:choose-frame");
    key(ImGuiKey_RightArrow);
    key(ImGuiKey_Escape);
    check(value == 2 && changes == 2 && !picker.open(),
          "Escape published pending frame navigation");
    click("sprite:choose-frame");
    canvas = box("sprite:sheet-canvas");
    const ImVec2 middle{(canvas.minimum.x + canvas.maximum.x) / 2,
                        (canvas.minimum.y + canvas.maximum.y) / 2};
    io.AddMousePosEvent(middle.x, middle.y);
    frame();
    io.AddMouseWheelEvent(0, 1);
    frame();
    frame();
    check(crop[2] - crop[0] < 1 && value == 2, "Sheet wheel did not zoom without publication");
    const auto zoomed = crop;
    io.AddMouseButtonEvent(1, true);
    frame();
    io.AddMousePosEvent(middle.x + 40, middle.y);
    frame();
    io.AddMouseButtonEvent(1, false);
    frame();
    check(crop != zoomed && value == 2, "Sheet pan failed or changed the caller value");
    key(ImGuiKey_End);
    check(crop[2] > .999f && value == 2, "Keyboard navigation did not reveal the last frame");
    click("sprite:cancel-frame");
    check(value == 2 && changes == 2, "Cancel published a pending frame");
    ready = false;
    click("sprite:choose-frame");
    check(!box("sprite:use-frame").enabled, "An unavailable image allowed frame publication");
    key(ImGuiKey_Enter);
    check(value == 2 && changes == 2, "Missing image accepted a keyboard frame");
    click("sprite:retry-sheet");
    check(retries == 1 && value == 2, "Retry bypassed image readiness or changed caller state");
    click("sprite:cancel-frame");
    ready = true;
    click("sprite:choose-frame");
    ++revision.generation;
    frame();
    check(!picker.open() && value == 2, "Image replacement retained a selectable stale chooser");
    click("sprite:choose-frame");
    owner = "scene/entity-two";
    frame();
    check(!picker.open() && value == 2, "Chooser applied to a different entity");
    click("sprite:choose-frame");
    context.grid = {2, 2};
    frame();
    check(!picker.open() && value == 2, "Changed grid retained a selectable stale chooser");
    context.grid = {4, 1};
    click("sprite:choose-frame");
    disabled = true;
    frame();
    key(ImGuiKey_Enter);
    check(!picker.open() && value == 2, "Read-only transition accepted a pending frame");
    disabled = false;
    click("sprite:choose-frame");
    draw_field = false;
    frame();
    check(!picker.open() && value == 2, "Disappearing field left an orphan popup");
    draw_field = true;
    image_width = image_height = 4096;
    context.grid = {4096, 4096};
    click("sprite:choose-frame");
    check(ImGui::GetDrawData()->TotalVtxCount < 16000 && value == 2,
          "Huge frame grid emitted a widget/rectangle for every cell");
    click("sprite:cancel-frame");
    test::observe_ui = false;
    test::ui_targets.clear();
    ImGui::DestroyContext();
}
} // namespace forge::test_sprite_picker
