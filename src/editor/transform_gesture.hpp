#pragma once
#include "blockout.hpp"
#include "camera.hpp"
#include <locale>
#include <sstream>
namespace forge {
// Transient, single-selection preview. Authored state changes only at accept().
class TransformGesture {
  public:
    enum class Mode { Rotate, Scale };
    bool active() const { return !entity_.empty(); }
    bool valid(const Scene& scene, const std::string& selection) const {
        return active() && revision_ == scene.revision() && entity_ == selection;
    }
    void cancel() { entity_.clear(); }
    Mode mode() const { return mode_; }
    int axis() const { return axis_; }
    Float3 value() const { return value_; }
    bool begin(const Scene& scene, const std::string& id, Mode mode, Float3 view_axis) {
        cancel();
        auto doc = scene.effective_document();
        for (const auto& e : doc.at("entities")) {
            if (e.at("id") != id || e.value("prefab", false) ||
                !e.at("components").contains("forge.position") ||
                !e.value("spatial_resolved", true))
                continue;
            owner_ = &scene;
            amount_ = mode == Mode::Scale ? 1 : 0;
            pending_.reset();
            error_.clear();
            entity_ = id;
            revision_ = scene.revision();
            mode_ = mode;
            axis_ = -1;
            original_ = e;
            view_axis_ = view_axis;
            value_ = read_xyz(e.at("components"), component(),
                              mode == Mode::Scale ? Float3{1, 1, 1} : Float3{});
            return true;
        }
        return false;
    }
    void constrain(int axis) {
        if (axis >= -1 && axis < 3)
            axis_ = axis;
    }
    bool update(float amount) {
        if (!active() || !std::isfinite(amount))
            return false;
        const auto previous = amount_;
        amount_ = amount;
        try {
            auto next = preview_authoring(*owner_, Json::array({command()}));
            const auto view = owner_->preview_document(next);
            const auto& e = blockout_entity(view, entity_);
            value_ = read_xyz(e.at("components"), component(),
                              mode_ == Mode::Scale ? Float3{1, 1, 1} : Float3{});
            pending_ = std::move(next);
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            amount_ = previous;
            error_ = e.what();
            return false;
        }
    }
    const std::string& error() const { return error_; }

    Json preview(const Json& source) const { return active() && pending_ ? *pending_ : source; }

    bool accept(Scene& scene) {
        if (!active())
            return false;
        if (revision_ != scene.revision()) {
            cancel();
            throw std::runtime_error("Transform cancelled: scene changed");
        }
        const auto cmd = command();
        const bool neutral =
            mode_ == Mode::Scale ? amount_ == 1 : std::remainder(amount_, 360.0f) == 0;
        cancel();
        if (neutral)
            return false;
        const auto result = apply_authoring(scene, Json::array({cmd}), scene.revision());
        return result.at("changed");
    }

  private:
    Json command() const {
        Json args = {{"entity", entity_}};
        if (mode_ == Mode::Scale) {
            if (amount_ <= 0)
                throw std::runtime_error("Scale must be positive");
            auto value = read_xyz(original_.at("components"), "forge.scale", {1, 1, 1});
            for (unsigned i = 0; i < 3; ++i)
                if (axis_ < 0 || axis_ == int(i))
                    value[i] *= amount_;
            args["value"] = {{"x", value[0]}, {"y", value[1]}, {"z", value[2]}};
            return {{"operation", "transform.scale"}, {"arguments", args}};
        }
        auto axis = view_axis_;
        if (axis_ >= 0) {
            axis = {};
            axis[axis_] = 1;
        }
        args["axis"] = {{"x", axis[0]}, {"y", axis[1]}, {"z", axis[2]}};
        args["degrees"] = amount_;
        return {{"operation", "transform.world_rotate"}, {"arguments", args}};
    }
    const Scene* owner_ = nullptr;
    float amount_ = 0;
    std::optional<Json> pending_;
    std::string error_;
    const char* component() const {
        return mode_ == Mode::Scale ? "forge.scale" : "forge.rotation";
    }
    std::string entity_;
    std::uint64_t revision_ = 0;
    Mode mode_ = Mode::Rotate;
    int axis_ = -1;
    Json original_;
    Float3 value_{}, view_axis_{};
};
namespace ui {
struct ModalTransform {
    TransformGesture gesture;
    ImVec2 start{}, size_{};
    std::string typed, feedback;
    int requested_tool = 0;
    void request(bool scale) { requested_tool = scale ? 2 : 1; }
    bool active() const { return gesture.active(); }
    void cancel() {
        gesture.cancel();
        requested_tool = 0;
    }
    Json preview(const Json& doc) const { return gesture.preview(doc); }
    void input(Scene& scene, const std::string& selected, const EditorCamera& camera, ImVec2 origin,
               ImVec2 size, bool hovered, bool allowed, std::string& status) {
        auto& io = ImGui::GetIO();
        if (active() &&
            (!allowed || !gesture.valid(scene, selected) || std::abs(size.x - size_.x) > .5f ||
             std::abs(size.y - size_.y) > .5f || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || !ImGui::IsWindowFocused())) {
            cancel();
            status = "Transform cancelled";
            return;
        }
        bool began = false;
        if (!active() && allowed && (hovered || requested_tool) && !io.WantTextInput &&
            !ImGui::IsAnyItemActive() && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
            !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2)) {
            const bool scale = requested_tool == 2 || ImGui::IsKeyPressed(ImGuiKey_S, false);
            const bool rotate = requested_tool == 1 || ImGui::IsKeyPressed(ImGuiKey_R, false);
            requested_tool = 0;
            if ((scale || rotate) && gesture.begin(scene, selected,
                                                   scale ? TransformGesture::Mode::Scale
                                                         : TransformGesture::Mode::Rotate,
                                                   camera.forward())) {
                ImGui::SetWindowFocus();
                start = io.MousePos;
                size_ = size;
                typed.clear();
                began = true;
            }
        }
        if (!active())
            return;
        const ImGuiKey axes[] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
        for (int i = 0; i < 3; ++i)
            if (ImGui::IsKeyPressed(axes[i], false))
                gesture.constrain(gesture.axis() == i ? -1 : i);
        if (!io.KeyCtrl && !io.KeyAlt) {
            for (int i = 0; i < 10; ++i)
                if ((ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + i), false) ||
                     ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_Keypad0 + i), false)) &&
                    typed.size() < 24)
                    typed += char('0' + i);
            if ((ImGui::IsKeyPressed(ImGuiKey_Period, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) &&
                typed.find('.') == std::string::npos)
                typed += '.';
            if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
                if (!typed.empty() && typed[0] == '-')
                    typed.erase(0, 1);
                else
                    typed.insert(0, "-");
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !typed.empty())
                typed.pop_back();
        }
        const bool scale = gesture.mode() == TransformGesture::Mode::Scale;
        float amount =
            scale ? std::exp(std::clamp((io.MousePos.x - start.x) / (120 * interface_scale), -12.0f,
                                        12.0f))
                  : (io.MousePos.x - start.x) * .5f / interface_scale;
        bool valid = true;
        if (!typed.empty()) {
            std::istringstream input(typed);
            input.imbue(std::locale::classic());
            valid = bool(input >> amount) && input.peek() == std::char_traits<char>::eof();
        }
        valid = valid && gesture.update(amount);
        const char* axis = gesture.axis() < 0 ? (scale ? "Uniform" : "View axis")
                                              : (gesture.axis() == 0   ? "X"
                                                 : gesture.axis() == 1 ? "Y"
                                                                       : "Z");
        feedback = std::string(scale ? "Scale | Local " : "Rotate | World ") + axis + " | " +
                   (typed.empty() ? std::to_string(amount) : typed) + (scale ? "x" : " deg") +
                   (valid ? " | Enter / click: apply | Esc / RMB: cancel"
                          : " | Invalid value; edit or Esc to cancel");
        if (!valid && !gesture.error().empty())
            feedback += " | " + gesture.error();
        if (!began && valid &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
             (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
            try {
                gesture.accept(scene);
                status = scale ? "Scale applied" : "Rotation applied";
            } catch (const std::exception& e) {
                cancel();
                status = e.what();
            }
        }
        (void)origin;
    }
    void draw(ImVec2 origin, ImVec2 size) const {
        if (!active())
            return;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        draw->AddRectFilled(
            origin,
            {origin.x + size.x, origin.y + ImGui::GetTextLineHeight() + 16 * interface_scale},
            IM_COL32(25, 32, 43, 245));
        draw->AddText({origin.x + 8 * interface_scale, origin.y + 8 * interface_scale},
                      IM_COL32(255, 220, 135, 255), feedback.c_str());
        draw->PopClipRect();
    }
};
} // namespace ui
} // namespace forge
