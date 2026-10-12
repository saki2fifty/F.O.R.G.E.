#include "builtins.hpp"
#include "editor/component_inspector.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto schema = detail::builtin_schema();
        for (const std::string type : {"forge.two_bone_ik", "forge.aim_ik"})
            for (const float scale : {.65f, 1.f, 1.5f, 2.f})
                for (const float window_width : {420.f, 1200.f}) {
                    const auto& codecs = detail::builtins();
                    const auto codec = std::find_if(codecs.begin(), codecs.end(),
                                                    [&](const auto& c) { return type == c.name; });
                    auto value = codec->defaults;
                    value["enabled"] = true;
                    const auto actor = EntityId::generate().str();
                    const Json effective = {
                        {"entities",
                         Json::array(
                             {{{"id", actor},
                               {"components",
                                {{type, value},
                                 {"forge.model_source", {{"model", AssetId::generate()}}},
                                 {"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                 {"project.unadmitted", {{"speed", 1}}}}}}})}};
                    const auto before = effective;
                    ImGui::CreateContext();
                    auto& io = ImGui::GetIO();
                    io.IniFilename = nullptr;
                    io.DisplaySize = {1400, 2000};
                    io.DeltaTime = 1.f / 60;
                    io.ConfigInputTrickleEventQueue = false;
                    unsigned char* pixels;
                    int width, height;
                    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                    ui::style(scale);
                    test::observe_ui = true;
                    ComponentInspector inspector;
                    unsigned submits = 0;
                    std::string sent_type, sent_property;
                    Json sent;
                    auto frame = [&] {
                        test::ui_targets.clear();
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                        ImGui::SetNextWindowSize({window_width, 1900}, ImGuiCond_Always);
                        ImGui::Begin("Inspector");
                        inspector.draw_runtime(
                            effective, schema, root, actor,
                            [&](const auto& key, const auto& field, const auto& v) {
                                ++submits;
                                sent_type = key;
                                sent_property = field;
                                sent = v;
                                return true;
                            },
                            false);
                        ImGui::End();
                        ImGui::Render();
                    };
                    frame();
                    frame();
                    require(
                        !test::ui_targets.contains("runtime-field:forge.local_translation:x") &&
                            !test::ui_targets.contains("runtime-field:project.unadmitted:speed"),
                        "Live rig Inspector opened arbitrary builtin/custom property tuning");
                    auto point = [&](const std::string& field) {
                        const auto box = test::ui_targets.at("runtime-field:" + type + ":" + field);
                        require(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                                    box.maximum.x <= box.clip_maximum.x &&
                                    box.minimum.y >= box.clip_minimum.y &&
                                    box.maximum.y <= box.clip_maximum.y,
                                "Live rig field clipped or disabled");
                        io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                            (box.minimum.y + box.maximum.y) * .5f);
                        frame();
                    };
                    point("enabled");
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                    require(submits == 1 && sent_type == type && sent_property == "enabled" &&
                                sent == false,
                            "Live rig checkbox did not submit its typed command");
                    point("target_x");
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
                    io.AddKeyEvent(ImGuiKey_A, true);
                    frame();
                    io.AddKeyEvent(ImGuiKey_A, false);
                    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
                    frame();
                    io.AddInputCharactersUTF8("0.75");
                    frame();
                    require(submits == 1, "Live rig numeric draft submitted before Enter");
                    io.AddKeyEvent(ImGuiKey_Enter, true);
                    frame();
                    io.AddKeyEvent(ImGuiKey_Enter, false);
                    frame();
                    require(submits == 2 && sent_type == type && sent_property == "target_x" &&
                                sent == .75,
                            "Live rig target did not submit on Enter");
                    require(effective == before, "Live Inspector changed its authored input");
                    ImGui::DestroyContext();
                }
        std::filesystem::remove_all(root);
        std::cout << "Actual Live reach/aim fields, Enter submissions, scale/width and isolation "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
