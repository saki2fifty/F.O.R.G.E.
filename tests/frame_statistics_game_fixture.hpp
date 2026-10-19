#pragma once
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace forge::test {
// Observer for the real game loop; no alternate sampler, renderer or scene owner.
struct FrameStatisticsGameFixture {
    unsigned stage = 0;
    std::uint64_t ticket = 0;
    Uint64 restore_at = 0;
    bool restored = false;
    Json records = Json::array();
    static void check(bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    }
    void poll(SDL_Window* window) {
        if (restore_at && SDL_GetTicks() >= restore_at) {
            check(SDL_RestoreWindow(window), "FPS fixture window restore failed");
            check(SDL_SyncWindow(window), "FPS fixture restore did not settle");
            restore_at = 0;
            restored = true;
        }
    }
    static Rml::Element* element(const char* id) {
        for (int c = 0; c < Rml::GetNumContexts(); ++c) {
            auto* context = Rml::GetContext(c);
            for (int d = 0; d < context->GetNumDocuments(); ++d) {
                auto* document = context->GetDocument(d);
                if (document->IsVisible())
                    if (auto* element = document->GetElementById(id))
                        return element;
            }
        }
        return nullptr;
    }
    static Rml::Element* panel() { return element("forge-frame-statistics"); }
    static std::string text() {
        auto* element = panel();
        check(element && element->GetNumChildren() == 1, "FPS game panel/text is missing");
        return static_cast<Rml::ElementText*>(element->GetChild(0))->GetText();
    }
    static bool reading() {
        double fps = 0, mean = 0, peak = 0;
        const auto label = text();
        if (label.find("--") != std::string::npos)
            return false;
        check(std::sscanf(label.c_str(), "FPS %lf | Frame %lf ms | Peak %lf ms", &fps, &mean,
                          &peak) == 3 &&
                  std::isfinite(fps) && std::isfinite(mean) && std::isfinite(peak) && fps > 0 &&
                  mean > 0 && peak + .01 >= mean &&
                  std::abs(fps * mean - 1000) < mean * .06 + fps * .006,
              "Actual presentation reading is invalid or internally inconsistent");
        return true;
    }
    template <class Capture, class Click>
    void frame(GameSession& game, Diligent::ITextureView* image, SDL_Window* window, bool& running,
               const std::filesystem::path& output, Capture&& capture, Click&& click) {
        if (!image)
            return;
        const auto& desc = image->GetTexture()->GetDesc();
        int width = 0, height = 0;
        check(SDL_GetWindowSizeInPixels(window, &width, &height), "FPS window size missing");
        if (desc.Width != unsigned(width) || desc.Height != unsigned(height))
            return;
        const auto record = [&](const char* name) {
            auto* element = panel();
            check(element, "FPS panel missing after window/scene change");
            const auto origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
            check(origin.x >= 0 && origin.y >= 0 && origin.x + size.x <= width + .5f &&
                      origin.y + size.y <= height + .5f,
                  "FPS game panel is outside the actual output");
            capture(name);
            records.push_back({{"image", name},
                               {"text", text()},
                               {"width", width},
                               {"height", height},
                               {"stage", stage}});
        };
        if (stage == 0 && reading()) {
            record("game-fps-windowed.ppm");
            auto* pause = element("pause");
            check(pause, "FPS input proof requires the authored Pause button");
            const auto button_at = pause->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto button_size = pause->GetBox().GetSize(Rml::BoxArea::Border);
            const auto panel_at = panel()->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto panel_size = panel()->GetBox().GetSize(Rml::BoxArea::Border);
            const float left = std::max(button_at.x, panel_at.x);
            const float top = std::max(button_at.y, panel_at.y);
            const float right = std::min(button_at.x + button_size.x, panel_at.x + panel_size.x);
            const float bottom = std::min(button_at.y + button_size.y, panel_at.y + panel_size.y);
            check(right > left && bottom > top, "FPS panel did not overlap the input probe");
            int logical_width = 0, logical_height = 0;
            check(SDL_GetWindowSize(window, &logical_width, &logical_height),
                  "FPS logical window dimensions missing");
            // Send SDL input through the overlap, accounting for high-density windows.
            click(window, (left + right) * .5f * logical_width / width,
                  (top + bottom) * .5f * logical_height / height);
            stage = 1;
        } else if (stage == 1 && game.status().at("state") == "paused") {
            record("game-fps-input-passed.ppm");
            check(SDL_SetWindowSize(window, 800, 600), "FPS resize failed");
            stage = 2;
        } else if (stage == 2 && width == 800 && height == 600) {
            record("game-fps-resized.ppm");
            check(SDL_SetWindowFullscreen(window, true), "FPS fullscreen request failed");
            check(SDL_SyncWindow(window), "FPS fullscreen did not settle");
            stage = 3;
        } else if (stage == 3 && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            record("game-fps-fullscreen.ppm");
            check(SDL_SetWindowFullscreen(window, false), "FPS windowed restore failed");
            check(SDL_SyncWindow(window), "FPS windowed restore did not settle");
            stage = 4;
        } else if (stage == 4 && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            auto candidate = game.active().scene.snapshot();
            for (auto& entity : candidate.at("entities"))
                entity.at("components").erase("forge.ui_document");
            ticket = game.prepare(candidate);
            stage = 5;
        } else if (stage == 5 && game.poll_preparation(ticket).ready) {
            game.activate(ticket, RuntimeClock::Clock::now(), false);
            stage = 6;
        } else if (stage == 6) {
            check(Rml::GetNumContexts() == 1 && Rml::GetContext(0)->GetNumDocuments() == 1,
                  "No-canvas game retained an authored UI or old candidate context");
            record("game-fps-no-canvas.ppm");
            check(SDL_MinimizeWindow(window), "FPS minimize request failed");
            check(SDL_SyncWindow(window) && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED),
                  "FPS window did not actually minimize");
            restore_at = SDL_GetTicks() + 250;
            stage = 7;
        } else if (stage == 7 && restored) {
            check(text().find("--") != std::string::npos,
                  "Minimized interval retained the old FPS reading");
            record("game-fps-resumed-pending.ppm");
            stage = 8;
        } else if (stage == 8 && reading()) {
            record("game-fps-resumed.ppm");
            atomic_write(output / "frame-statistics-result.json",
                         Json{{"actual_presentations", true},
                              {"input_passed_to_authored_ui", true},
                              {"no_authored_canvas", true},
                              {"resized", true},
                              {"fullscreen", true},
                              {"minimize_reset", true},
                              {"captures", records}}
                             .dump(2));
            running = false;
        }
    }
};
} // namespace forge::test
