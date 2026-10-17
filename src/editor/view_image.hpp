#pragma once
#include "../presentation_diligent.hpp"
#include "../view_image.hpp"
#include "editor_state.hpp"
#include "file_dialog.hpp"
#include "ui_probe.hpp"
#include <future>
namespace forge::ui {
// One explicit image export. No authored state or persistent panel.
class ViewImageTask {
  public:
    enum class View { Scene, Game };
    bool busy() const { return dialog_.busy() || pending_ || copied_ || job_.valid(); }
    const std::string& status() const { return status_; }
    void choose(View view, SDL_Window* window, const std::filesystem::path& project,
                AssetId scene) {
        if (busy())
            return;
        project_ = project;
        scene_ = scene;
        view_ = view;
        dialog_.show(FileDialog::Kind::SaveImage, window,
                     path_utf8(project / (view == View::Scene ? "Scene.png" : "Game.png")));
    }
    // Completed picker selection. This adapter also accepts explicit editor requests;
    // an existing filename is already confirmed by the calling file picker.
    bool selected_path(View view, const std::filesystem::path& path,
                       const std::filesystem::path& project, AssetId scene, std::string& message) {
        if (busy())
            return false;
        try {
            project_ = project;
            scene_ = scene;
            view_ = view;
            destination_ = std::filesystem::absolute(path).lexically_normal();
            const bool append = destination_.extension().empty();
            if (append)
                destination_ += ".png";
            auto extension = path_utf8(destination_.extension());
            for (auto& c : extension)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            if (extension != ".png")
                throw std::runtime_error("Choose a PNG filename ending in .png");
            if (std::filesystem::is_directory(destination_))
                throw std::runtime_error("Choose an image file, not a folder");
            // An appended suffix can name a file the native picker did not confirm.
            confirm_ = append && std::filesystem::exists(destination_);
            pending_ = true;
            publish_status(confirm_ ? "Confirm replacing the PNG image"
                                    : "Waiting for the rendered view image",
                           message);
            return true;
        } catch (const std::exception& e) {
            pending_ = confirm_ = false;
            fail(e.what(), message);
            return false;
        }
    }
    void poll(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              const std::filesystem::path& project, AssetId scene, std::string& message) {
        try {
            if (job_.valid() &&
                job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto path = job_.get();
                publish_status("Image saved: " + path, message);
                if (editor_context)
                    editor_context->problems.resolve("view-image");
            }
            if (auto result = dialog_.take()) {
                if (!result->error.empty())
                    throw std::runtime_error(result->error);
                if (result->path.empty())
                    publish_status("Image save cancelled", message);
                else if (project != project_ || scene != scene_)
                    publish_status("Image save cancelled because the Scene changed", message);
                else
                    selected_path(view_, std::filesystem::u8path(result->path), project, scene,
                                  message);
            }
            if (pending_ && (project != project_ || scene != scene_)) {
                pending_ = confirm_ = false;
                publish_status("Image save cancelled because the Scene changed", message);
            }
            if (copied_) {
                auto pixels = presentation.take_readback(context);
                if (pixels) {
                    copied_ = false;
                    const auto destination = destination_;
                    job_ =
                        std::async(std::launch::async, [pixels = std::move(*pixels), destination] {
                            save_view_png(destination, pixels.width, pixels.height, pixels.rgba);
                            return path_utf8(destination);
                        });
                    publish_status("Saving PNG image…", message);
                }
            }
        } catch (const std::exception& e) {
            pending_ = confirm_ = copied_ = false;
            fail(e.what(), message);
        }
    }
    void capture(View view, DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                 Diligent::ITextureView* texture, std::string& message) {
        if (!pending_ || confirm_ || view != view_)
            return;
        try {
            presentation.readback(context, texture);
            pending_ = false;
            copied_ = true;
            publish_status("Reading the rendered view image", message);
        } catch (const std::exception& e) {
            pending_ = false;
            fail(e.what(), message);
        }
    }
    void unavailable(View view, std::string& message) {
        if (pending_ && view_ == view) {
            pending_ = confirm_ = false;
            fail("Open the requested view and ensure it has a rendered image before saving",
                 message);
        }
    }
    void draw(std::string& message) {
        if (confirm_)
            ImGui::OpenPopup("Replace PNG image?");
        if (ImGui::BeginPopupModal("Replace PNG image?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!confirm_) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
            ImGui::TextUnformatted("Replace this existing image?");
            ImGui::TextWrapped("%s", path_utf8(destination_).c_str());
            if (button("Replace", "Replace the existing PNG after the new image is ready.")) {
                confirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (button("Cancel", "Keep the existing image.")) {
                pending_ = confirm_ = false;
                publish_status("Image save cancelled", message);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    FileDialog dialog_;
    std::filesystem::path project_, destination_;
    AssetId scene_;
    std::string status_;
    View view_ = View::Scene;
    bool pending_ = false, copied_ = false, confirm_ = false;
    std::future<std::string> job_;
    void publish_status(std::string text, std::string& message) {
        status_ = message = std::move(text);
        if (editor_context)
            editor_context->record_status("Image export", status_);
    }
    void fail(const std::string& text, std::string& message) {
        publish_status("Image save failed: " + text, message);
        report_error("view-image", status_);
    }
};
} // namespace forge::ui
