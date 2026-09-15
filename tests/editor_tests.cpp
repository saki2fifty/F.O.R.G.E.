#include "camera.hpp"
#include "play.hpp"
#include "widgets.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected runtime and fault worker paths");
        ImGui::CreateContext();
        forge::ui::style(1.0f);
        const auto padding = ImGui::GetStyle().FramePadding.x;
        for (int i = 0; i < 20; ++i) {
            forge::ui::style(2.0f);
            forge::ui::style(1.0f);
        }
        require(ImGui::GetStyle().FramePadding.x == padding, "Scale accumulated rounding drift");
        forge::ui::style(std::numeric_limits<float>::infinity());
        require(forge::ui::interface_scale == 1, "Nonfinite preference not reset");
        forge::ui::style(10);
        require(forge::ui::interface_scale == 2, "Scale maximum failed");
        forge::ui::style(-1);
        require(forge::ui::interface_scale == 0.65f, "Scale minimum failed");
        ImGui::DestroyContext();
        forge::Scene authored;
        auto original = authored.document();
        original["entities"].push_back(
            {{"id", "test"},
             {"name", "Test"},
             {"components", {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 0}}}}}});
        authored.replace(original);
        {
            forge::EditorCamera camera;
            const auto before = original;
            require(camera.frame(original, "test", 0.4f), "Frame selected failed");
            const auto portrait_distance = camera.distance;
            require(camera.frame(original, "test", 2.0f), "Landscape frame failed");
            require(portrait_distance > camera.distance, "Framing ignored narrow aspect");
            camera.orbit(130, 70);
            require(camera.frame(original, "", 0.4f), "Fit scene failed");
            const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
                       forward = camera.forward();
            for (float x : {-0.5f, 0.5f})
                for (float y : {0.5f, 1.5f})
                    for (float z : {-0.5f, 0.5f}) {
                        const forge::EditorCamera::Vec offset{x - eye[0], y - eye[1], z - eye[2]};
                        auto dot = [&](auto axis) {
                            return offset[0] * axis[0] + offset[1] * axis[1] + offset[2] * axis[2];
                        };
                        const auto depth = dot(forward);
                        require(depth > camera.near_plane, "Framed cube crosses near plane");
                        require(std::abs(dot(right) * camera.focal / (depth * 0.4f)) < 1 &&
                                    std::abs(dot(up) * camera.focal / depth) < 1,
                                "Framed cube is clipped");
                    }
            const auto target = camera.target;
            require(!camera.frame(original, "missing", 1), "Missing frame target accepted");
            require(camera.target == target, "Failed frame moved camera");
            camera.pan(10, 0, 800);
            require(camera.target != target, "Pan did not move target");
            for (int i = 0; i < 100; ++i)
                camera.zoom(20);
            require(camera.distance == 0.25f, "Zoom minimum failed");
            for (int i = 0; i < 100; ++i)
                camera.zoom(-20);
            require(camera.distance == 100000, "Zoom maximum failed");
            camera.orbit(0, 100000);
            require(camera.pitch == 1.5f, "Orbit crossed pole");
            camera.zoom(std::numeric_limits<float>::quiet_NaN());
            require(std::isfinite(camera.distance), "Nonfinite wheel damaged camera");
            require(original == before, "Camera mutated scene");
        }
        forge::PlaySession play;
        play.start(argv[1], original);
        const auto deadline = SDL_GetTicks() + 1000;
        while (play.active() && SDL_GetTicks() < deadline) {
            play.pump();
            SDL_Delay(1);
        }
        require(play.active(), "Runtime unexpectedly stopped");
        require(play.status().find("Playing") != std::string::npos, "Play handshake failed");
        require(play.snapshot() == original, "Play snapshot round trip failed");
        require(authored.document() == original, "Play changed authoring");
        auto edited = original;
        edited["entities"][0]["components"]["forge.position"]["x"] = 10;
        authored.edit(edited);
        play.pump();
        require(play.snapshot() == original, "Authoring edit leaked into running play scene");
        play.stop();
        require(authored.document() == edited, "Stop discarded authored edits");
        require(!play.active(), "Stop failed");
        play.start("/missing-forge-runtime", original);
        require(!play.active(), "Missing runtime accepted");
        for (const char* mode : {"crash", "malformed", "hang"}) {
            auto request = original;
            request["test_failure"] = mode;
            play.start(argv[2], request);
            const auto timeout = SDL_GetTicks() + 6500;
            while (play.active() && SDL_GetTicks() < timeout) {
                play.pump();
                SDL_Delay(1);
            }
            require(!play.active(), "Faulty runtime was not stopped");
            require(play.log().find("fault-worker diagnostic") != std::string::npos,
                    "Runtime stderr was not captured");
            require(play.status().find("Authored scene is safe") != std::string::npos,
                    "Fault diagnostic missing");
        }
        play.start(argv[1], original);
        require(play.active(), "Restart after fault failed");
        play.stop();
        std::cout << "Editor scale and process tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
