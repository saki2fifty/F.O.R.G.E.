#pragma once
#include "../asset_bytes.hpp"
#include "../asset_storage.hpp"
#include "../presentation_diligent.hpp"
#include "../view_image.hpp"
#include "editor_state.hpp"
#include "file_dialog.hpp"
#include "sequence_frame_output.hpp"
#include "sequence_frame_plan.hpp"
#include "sequence_frame_source.hpp"
#include "ui_probe.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <forge/build.hpp>
#include <future>
#include <optional>
#include <utility>
namespace forge::ui {
// One explicit image operation: a single view or sequential sequence frames.
// No authored state or persistent panel.
class ViewImageTask {
    enum class FileWork { Image, PrepareFrames, FrameReceipt };
    struct FrameReceipt {
        SequenceFrameSourceInfo source;
        SequenceFramePlan plan;
        SequenceFrameOutput output;
        std::filesystem::path folder;
        std::string source_digest, state, error, reason;
        unsigned completed = 0, width = 0, height = 0;
    };
    struct FrameBatch : FrameReceipt {
        SequenceFrameSource* timeline = nullptr;
        std::uint64_t token = 0;
        bool owns_folder = false, cancelled = false, finishing = false, discarding = false;
        unsigned ready_frames = 0;
        bool configuring = false;
        int open_after = 0;
        SDL_Window* window = nullptr;
        char parent[2048]{}, name[128] = "SequenceFrames";
        std::string validation;
        std::chrono::steady_clock::time_point deadline;
    };

  public:
    enum class View { Scene, Game };
    bool busy() const { return dialog_.busy() || pending_ || copied_ || job_.valid() || frames_; }
    bool sequence_active() const { return bool(frames_); }
    std::optional<SequenceFrameExtent> sequence_extent() const {
        if (!frames_ || frames_->finishing || frames_->cancelled || !frames_->error.empty() ||
            !frames_->token || !frames_->timeline->frame_export_current(frames_->token))
            return {};
        return frames_->output.extent();
    }
    ~ViewImageTask() {
        // Main declares this task after Timeline. Shutdown waits only for the one
        // existing file task; presentation retains any GPU ticket until retirement.
        if (!frames_)
            return;
        try {
            if (job_.valid()) {
                try {
                    auto result = job_.get();
                    if (file_work_ == FileWork::PrepareFrames) {
                        frames_->owns_folder = true;
                        frames_->source_digest = std::move(result);
                    } else if (file_work_ == FileWork::Image)
                        ++frames_->completed;
                    else
                        return; // Receipt was already accepted; preview was restored.
                } catch (const std::exception& e) {
                    frames_->error = e.what();
                }
            }
            release_frame_source();
            if (frames_->owns_folder && !frames_->finishing) {
                frames_->state = frames_->error.empty() ? "cancelled" : "failed";
                frames_->reason = "Editor closed";
                write_frame_receipt(*frames_);
            }
        } catch (...) {
            // Shutdown cannot promise successful disk IO; it must still retire.
        }
    }
    bool begin_sequence(SequenceFrameSource& timeline, const SequenceFramePlan& plan,
                        const std::filesystem::path& folder, const std::filesystem::path& project,
                        AssetId scene, std::string& message,
                        const SequenceFrameOutput& output = {}) {
        if (busy())
            return false;
        frames_.emplace();
        frames_->timeline = &timeline;
        frames_->plan = plan;
        frames_->output = output;
        try {
            project_ = project;
            scene_ = scene;
            view_ = View::Game;
            frames_->source = timeline.frame_export_info();
            plan.validate(frames_->source.duration);
            output.validate();
            if (const auto extent = output.extent()) {
                frames_->width = extent->width;
                frames_->height = extent->height;
            }
            if (frames_->source.controller.scene != scene || !frames_->source.catalog ||
                frames_->source.catalog->project_root() != project)
                throw std::runtime_error(
                    "The selected sequence belongs to another project or Scene");
            frames_->folder = std::filesystem::absolute(folder).lexically_normal();
            auto authored = timeline.frame_export_document();
            const auto destination = frames_->folder;
            file_work_ = FileWork::PrepareFrames;
            job_ = std::async(std::launch::async, [destination, authored = std::move(authored)] {
                const auto text = authored.dump();
                auto digest = asset_detail::content_digest(
                    std::as_bytes(std::span(text.data(), text.size())));
                if (!std::filesystem::create_directory(destination))
                    throw std::runtime_error("Choose a new folder; the destination already exists");
                return digest;
            });
            publish_status("Preparing sequence frame export", message);
            return true;
        } catch (const std::exception& e) {
            fail(e.what(), message);
            return false;
        }
    }
    bool choose_sequence(SequenceFrameSource& source, SDL_Window* window,
                         const std::filesystem::path& project, AssetId scene,
                         std::string& message) {
        if (busy())
            return false;
        frames_.emplace();
        frames_->timeline = &source;
        frames_->window = window;
        frames_->configuring = true;
        frames_->open_after = ImGui::GetFrameCount() + 1;
        try {
            project_ = project;
            scene_ = scene;
            view_ = View::Game;
            frames_->source = source.frame_export_info();
            const auto duration = frames_->source.duration;
            if (!std::isfinite(duration) || duration <= 0 ||
                frames_->source.controller.scene != scene || !frames_->source.catalog ||
                frames_->source.catalog->project_root() != project)
                throw std::runtime_error("Choose a ready sequence in this project and Scene");
            frames_->plan.frame_count = static_cast<int>(std::min<double>(
                SequenceFramePlan::maximum_frames, std::max(1., std::ceil(duration * 30))));
            frames_->plan.validate(duration);
            const auto parent = path_utf8(project.parent_path());
            if (parent.size() >= sizeof(frames_->parent))
                throw std::runtime_error("Output parent path exceeds the editor field limit");
            std::memcpy(frames_->parent, parent.c_str(), parent.size() + 1);
            frames_->token = source.begin_frame_export();
            source.sample_frame_export(frames_->token, 0);
            frame_popup_ = true;
            publish_status("Choose sequence frame export settings", message);
            return true;
        } catch (const std::exception& e) {
            try {
                release_frame_source();
            } catch (...) {
            }
            frames_.reset();
            publish_status("Frame export failed: " + std::string(e.what()), message);
            report_error("view-image", status_);
            return false;
        }
    }
    void cancel_sequence(std::string& message) {
        if (frames_ && !frames_->finishing) {
            stop_frames("Cancelled by user");
            publish_status("Cancelling sequence frame export…", message);
        }
    }
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
            if (frames_ && !frames_->finishing) {
                if (project != project_ || scene != scene_ ||
                    (frames_->token && !frames_->timeline->frame_export_current(frames_->token)))
                    stop_frames("Sequence or project changed");
                if (pending_ && std::chrono::steady_clock::now() >= frames_->deadline)
                    throw std::runtime_error(
                        "Sequence frame resources did not become ready within 60 seconds");
            }
            if (job_.valid() &&
                job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto result = job_.get();
                if (!frames_) {
                    publish_status("Image saved: " + result, message);
                    if (editor_context)
                        editor_context->problems.resolve("view-image");
                } else if (file_work_ == FileWork::FrameReceipt) {
                    const auto error = frames_->error;
                    const auto reason = frames_->reason;
                    const bool cancelled = frames_->cancelled;
                    const auto count = frames_->completed;
                    frames_.reset();
                    publish_status(!error.empty() ? "Frame export failed: " + error
                                   : cancelled    ? "Frame export cancelled: " + reason
                                                  : "Sequence frames saved: " + result + " (" +
                                                        std::to_string(count) + " frames)",
                                   message);
                    if (error.empty() && !cancelled && editor_context)
                        editor_context->problems.resolve("view-image");
                } else {
                    if (file_work_ == FileWork::PrepareFrames) {
                        frames_->owns_folder = true;
                        frames_->source_digest = std::move(result);
                    } else
                        ++frames_->completed;
                    if (!frames_->cancelled && frames_->error.empty())
                        next_frame(message);
                }
            }
            if (auto result = dialog_.take()) {
                if (result->kind == FileDialog::Kind::SequenceFramesParent) {
                    if (frames_ && frames_->configuring) {
                        if (!result->error.empty())
                            frames_->validation = result->error;
                        else if (!result->path.empty()) {
                            if (result->path.size() >= sizeof(frames_->parent))
                                frames_->validation = "Selected folder path is too long";
                            else {
                                std::memcpy(frames_->parent, result->path.c_str(),
                                            result->path.size() + 1);
                                frames_->validation.clear();
                            }
                        }
                    }
                } else {
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
            }
            if (!frames_ && pending_ && (project != project_ || scene != scene_)) {
                pending_ = confirm_ = false;
                publish_status("Image save cancelled because the Scene changed", message);
            }
            if (frames_ && (frames_->cancelled || !frames_->error.empty()) && copied_ &&
                !frames_->discarding) {
                presentation.cancel_readback();
                frames_->discarding = true;
            }
            if (copied_) {
                auto pixels = presentation.take_readback(context);
                if (!presentation.readback_pending())
                    copied_ = false;
                if (pixels && (!frames_ || (!frames_->cancelled && frames_->error.empty()))) {
                    if (frames_) {
                        if (!frames_->width) {
                            frames_->width = pixels->width;
                            frames_->height = pixels->height;
                        } else if (frames_->width != pixels->width ||
                                   frames_->height != pixels->height)
                            throw std::runtime_error(
                                "Rendered frame dimensions changed during export");
                    }
                    const auto destination = destination_;
                    file_work_ = FileWork::Image;
                    job_ =
                        std::async(std::launch::async, [pixels = std::move(*pixels), destination] {
                            save_view_png(destination, pixels.width, pixels.height, pixels.rgba);
                            return path_utf8(destination);
                        });
                    publish_status(frames_ ? "Saving sequence frame " +
                                                 std::to_string(frames_->completed + 1) + " / " +
                                                 std::to_string(frames_->plan.frame_count)
                                           : "Saving PNG image…",
                                   message);
                }
            }
            if (frames_ && !frames_->finishing && !job_.valid() && !copied_ &&
                (frames_->cancelled || !frames_->error.empty() ||
                 (!frames_->configuring &&
                  frames_->completed == static_cast<unsigned>(frames_->plan.frame_count))))
                finish_frames(message);
        } catch (const std::exception& e) {
            if (!frames_)
                pending_ = confirm_ = copied_ = false;
            fail(e.what(), message);
        }
    }
    void capture(View view, DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                 Diligent::ITextureView* texture, std::string& message,
                 bool resources_ready = true) {
        if (frames_ && frames_->configuring && view == View::Game) {
            if (resources_ready && frames_->output.valid() &&
                frames_->timeline->frame_export_current(frames_->token)) {
                const auto& desc = texture->GetTexture()->GetDesc();
                if (const auto extent = frames_->output.extent();
                    extent && (desc.Width != extent->width || desc.Height != extent->height)) {
                    frames_->ready_frames = 0;
                    return;
                }
                if (frames_->width != desc.Width || frames_->height != desc.Height)
                    frames_->ready_frames = 0;
                frames_->width = desc.Width;
                frames_->height = desc.Height;
                frames_->ready_frames = std::min(3u, frames_->ready_frames + 1);
            } else
                frames_->ready_frames = 0;
            return;
        }
        if (!pending_ || confirm_ || view != view_)
            return;
        try {
            if (frames_) {
                if (!frames_->timeline->frame_export_current(frames_->token)) {
                    stop_frames("Sequence changed before rendering its frame");
                    return;
                }
                if (const auto extent = frames_->output.extent()) {
                    const auto& desc = texture->GetTexture()->GetDesc();
                    if (desc.Width != extent->width || desc.Height != extent->height)
                        throw std::runtime_error(
                            "Game preview did not render the requested fixed output size");
                }
                frames_->ready_frames = resources_ready ? frames_->ready_frames + 1 : 0;
                if (frames_->ready_frames < 3)
                    return;
            }
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
        if (frames_ && frames_->configuring && view == View::Game) {
            frames_->ready_frames = 0;
            return;
        }
        if (pending_ && view_ == view) {
            if (frames_) {
                stop_frames("Game preview view is unavailable");
                publish_status("Cancelling sequence frame export…", message);
                return;
            }
            pending_ = confirm_ = false;
            fail("Open the requested view and ensure it has a rendered image before saving",
                 message);
        }
    }
    void draw(std::string& message) {
        // Let the existing Game focus route activate its tab before opening a modal.
        if (frames_ && frame_popup_ && ImGui::GetFrameCount() > frames_->open_after)
            ImGui::OpenPopup("Export sequence frames");
        const auto available = ImGui::GetMainViewport()->WorkSize;
        const float scale = ImGui::GetFontSize() / 13.f;
        if (ImGui::IsPopupOpen("Export sequence frames"))
            ImGui::SetNextWindowSizeConstraints(
                {0, 0}, {std::max(240.f, available.x - 32), std::max(180.f, available.y - 32)});
        if (ImGui::BeginPopupModal("Export sequence frames", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!frames_) {
                frame_popup_ = false;
                ImGui::CloseCurrentPopup();
            } else if (frames_->configuring) {
                ImGui::TextUnformatted("Sequence preview → numbered PNG frames");
                ImGui::TextWrapped("Choose a new folder. Your previous preview position returns "
                                   "when export ends.");
                ImGui::BeginChild(
                    "Frame settings",
                    {std::max(220.f, std::min(620.f * scale, available.x - 96.f * scale)),
                     std::max(80.f, std::min(420.f * scale, available.y - 300.f * scale))},
                    ImGuiChildFlags_Borders);
                ImGui::PushItemWidth(-FLT_MIN);
                ImGui::TextUnformatted("Start time (s)");
                bool changed =
                    ImGui::InputDouble("##frame-start", &frames_->plan.start_seconds, 0, 0, "%.6f");
                FORGE_UI_PROBE("frame-export:start-time");
                ImGui::TextUnformatted("Frames per second");
                changed |= ImGui::InputDouble("##frame-fps", &frames_->plan.frames_per_second, 0, 0,
                                              "%.6f");
                FORGE_UI_PROBE("frame-export:fps");
                ImGui::TextUnformatted("Frame count");
                changed |= ImGui::InputInt("##frame-count", &frames_->plan.frame_count);
                FORGE_UI_PROBE("frame-export:count");
                bool output_changed = ImGui::Checkbox("Fixed output size", &frames_->output.fixed);
                FORGE_UI_PROBE("frame-export:fixed-size");
                if (frames_->output.fixed) {
                    const char* labels[] = {"720p (1280 × 720)", "1080p (1920 × 1080)",
                                            "Square (1024 × 1024)"};
                    const int widths[] = {1280, 1920, 1024}, heights[] = {720, 1080, 1024};
                    const char* selected = "Custom";
                    for (unsigned i = 0; i < 3; ++i)
                        if (frames_->output.width == widths[i] &&
                            frames_->output.height == heights[i])
                            selected = labels[i];
                    ImGui::TextUnformatted("Size preset");
                    const bool presets = ImGui::BeginCombo("##frame-size-preset", selected);
                    FORGE_UI_PROBE("frame-export:size-preset");
                    if (presets) {
                        for (unsigned i = 0; i < 3; ++i) {
                            if (ImGui::Selectable(labels[i], selected == labels[i])) {
                                frames_->output.width = widths[i];
                                frames_->output.height = heights[i];
                                output_changed = true;
                            }
                            FORGE_UI_PROBE("frame-export:size-preset:" + std::to_string(i));
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextUnformatted("Width (pixels)");
                    output_changed |= ImGui::InputInt("##frame-width", &frames_->output.width);
                    FORGE_UI_PROBE("frame-export:width");
                    ImGui::TextUnformatted("Height (pixels)");
                    output_changed |= ImGui::InputInt("##frame-height", &frames_->output.height);
                    FORGE_UI_PROBE("frame-export:height");
                    ImGui::TextWrapped(
                        "The Game preview fits this image without stretching. "
                        "Keep Game open; resizing its panel keeps this output size.");
                }
                if (output_changed)
                    frames_->ready_frames = 0;
                ImGui::TextUnformatted("Parent folder");
                if (ImGui::InputText("##frame-parent", frames_->parent, sizeof(frames_->parent)))
                    frames_->validation.clear();
                FORGE_UI_PROBE("frame-export:parent");
                ImGui::BeginDisabled(dialog_.busy());
                if (button("Browse…", "Choose the parent for a new image folder."))
                    dialog_.show(FileDialog::Kind::SequenceFramesParent, frames_->window,
                                 frames_->parent);
                FORGE_UI_PROBE("frame-export:browse");
                ImGui::EndDisabled();
                ImGui::TextUnformatted("New folder name");
                ImGui::InputText("##frame-name", frames_->name, sizeof(frames_->name));
                FORGE_UI_PROBE("frame-export:name");
                ImGui::PopItemWidth();
                bool valid = true;
                std::filesystem::path folder;
                std::string validation;
                try {
                    frames_->plan.validate(frames_->source.duration);
                    if (changed) {
                        frames_->timeline->sample_frame_export(frames_->token,
                                                               frames_->plan.sample(0));
                        frames_->ready_frames = 0;
                    }
                    frames_->output.validate();
                    const auto name = std::filesystem::u8path(frames_->name);
                    if (name.empty() || name.has_parent_path() || name == "." || name == ".." ||
                        !frames_->parent[0])
                        throw std::runtime_error("Choose a parent and one new folder name");
                    folder =
                        std::filesystem::absolute(std::filesystem::u8path(frames_->parent) / name)
                            .lexically_normal();
                } catch (const std::exception& e) {
                    validation = e.what();
                    valid = false;
                }
                if (!validation.empty())
                    ImGui::TextWrapped("%s", validation.c_str());
                if (!frames_->validation.empty())
                    ImGui::TextWrapped("%s", frames_->validation.c_str());
                if (valid)
                    ImGui::TextWrapped("Samples %.6f–%.6f s | Sequence duration %.6f s",
                                       frames_->plan.sample(0),
                                       frames_->plan.sample(frames_->plan.frame_count - 1),
                                       frames_->source.duration);
                if (frames_->ready_frames < 3)
                    ImGui::TextWrapped("Waiting for a ready Game preview with an enabled camera. "
                                       "Check Problems for rendering errors.");
                ImGui::EndChild();
                if (!frames_->output.valid())
                    ImGui::TextUnformatted("Output: choose valid dimensions");
                else if (frames_->width)
                    ImGui::Text("Output: %u × %u | RGBA PNG", frames_->width, frames_->height);
                else
                    ImGui::TextUnformatted("Output: waiting for Game preview");
                ImGui::BeginDisabled(!valid || dialog_.busy() || frames_->ready_frames < 3);
                if (button("Start export",
                           "Write each sampled preview to its own PNG in the new folder.")) {
                    try {
                        auto authored = frames_->timeline->frame_export_document();
                        frames_->folder = folder;
                        frames_->configuring = false;
                        file_work_ = FileWork::PrepareFrames;
                        job_ = std::async(
                            std::launch::async, [folder, authored = std::move(authored)] {
                                const auto text = authored.dump();
                                auto digest = asset_detail::content_digest(
                                    std::as_bytes(std::span(text.data(), text.size())));
                                if (!std::filesystem::create_directory(folder))
                                    throw std::runtime_error(
                                        "Choose a new folder; the destination already exists");
                                return digest;
                            });
                        publish_status("Preparing sequence frame export", message);
                    } catch (const std::exception& e) {
                        fail(e.what(), message);
                    }
                }
                FORGE_UI_PROBE("frame-export:start");
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (frames_ &&
                    button("Cancel", "Restore the preview without starting this export."))
                    cancel_sequence(message);
                FORGE_UI_PROBE("frame-export:cancel");
            } else {
                ImGui::TextWrapped("%s", status_.c_str());
                ImGui::ProgressBar(float(frames_->completed) / float(frames_->plan.frame_count));
                ImGui::BeginDisabled(frames_->finishing || frames_->cancelled ||
                                     !frames_->error.empty());
                if (button(
                        "Cancel export",
                        "Stop after the current accepted file operation; keep completed images."))
                    cancel_sequence(message);
                FORGE_UI_PROBE("frame-export:cancel");
                ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }
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
    std::optional<FrameBatch> frames_;
    FileWork file_work_ = FileWork::Image;
    bool frame_popup_ = false;
    FileDialog dialog_;
    std::filesystem::path project_, destination_;
    AssetId scene_;
    std::string status_;
    View view_ = View::Scene;
    bool pending_ = false, copied_ = false, confirm_ = false;
    std::future<std::string> job_;
    void stop_frames(std::string reason) {
        frames_->cancelled = true;
        frames_->reason = std::move(reason);
        pending_ = confirm_ = false;
    }
    void release_frame_source() {
        if (frames_ && frames_->token) {
            const auto token = std::exchange(frames_->token, 0);
            frames_->timeline->end_frame_export(token);
        }
    }
    void next_frame(std::string& message) {
        if (frames_->completed == static_cast<unsigned>(frames_->plan.frame_count))
            return;
        if (!frames_->token) {
            if (!frames_->timeline->frame_export_ready()) {
                stop_frames("Sequence changed before capture");
                return;
            }
            const auto current = frames_->timeline->frame_export_info();
            if (current.controller != frames_->source.controller ||
                current.revision != frames_->source.revision ||
                current.preview_generation != frames_->source.preview_generation) {
                stop_frames("Sequence preview reopened before capture");
                return;
            }
            frames_->token = frames_->timeline->begin_frame_export();
        }
        frames_->timeline->sample_frame_export(frames_->token,
                                               frames_->plan.sample(frames_->completed));
        destination_ = frames_->folder / frames_->plan.filename(frames_->completed);
        frames_->ready_frames = 0;
        frames_->discarding = false;
        frames_->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        pending_ = true;
        publish_status("Rendering sequence frame " + std::to_string(frames_->completed + 1) +
                           " / " + std::to_string(frames_->plan.frame_count),
                       message);
    }
    static void write_frame_receipt(const FrameReceipt& receipt) {
        const auto catalog = receipt.source.catalog->document().dump();
        Json document{{"format", "forge.sequence-preview-frames"},
                      {"version", 1},
                      {"state", receipt.state},
                      {"engine_source", source_commit},
                      {"build_id", build_id},
                      {"scene", receipt.source.controller.scene},
                      {"sequence", receipt.source.controller.entity},
                      {"scene_revision", receipt.source.revision},
                      {"source_digest", receipt.source_digest},
                      {"catalog_digest", asset_detail::content_digest(std::as_bytes(
                                             std::span(catalog.data(), catalog.size())))},
                      {"start_seconds", receipt.plan.start_seconds},
                      {"fps", receipt.plan.frames_per_second},
                      {"planned_frames", receipt.plan.frame_count},
                      {"completed_frames", receipt.completed},
                      {"width", receipt.width},
                      {"height", receipt.height},
                      {"size_mode", receipt.output.fixed ? "fixed" : "current_view"},
                      {"pixels", "RGBA8 display-encoded sRGB PNG"},
                      {"error", receipt.error},
                      {"reason", receipt.reason},
                      {"frames", Json::array()}};
        for (unsigned i = 0; i < receipt.completed; ++i)
            document["frames"].push_back(
                {{"file", receipt.plan.filename(i)}, {"time_seconds", receipt.plan.sample(i)}});
        asset_storage::replace(receipt.folder / "frames.json", document.dump(2));
    }
    void finish_frames(std::string& message) {
        pending_ = confirm_ = false;
        try {
            release_frame_source();
        } catch (const std::exception& e) {
            frames_->error = e.what();
            report_error("view-image", frames_->error);
        }
        if (!frames_->owns_folder) {
            const auto error = frames_->error;
            frames_.reset();
            publish_status(error.empty() ? "Frame export cancelled"
                                         : "Frame export failed: " + error,
                           message);
            return;
        }
        frames_->finishing = true;
        frames_->state = !frames_->error.empty() ? "failed"
                         : frames_->cancelled    ? "cancelled"
                                                 : "complete";
        FrameReceipt receipt =
            *frames_; // Detached values; no Timeline/native pointer reaches the worker.
        file_work_ = FileWork::FrameReceipt;
        job_ = std::async(std::launch::async, [receipt = std::move(receipt)] {
            write_frame_receipt(receipt);
            return path_utf8(receipt.folder);
        });
        publish_status("Writing sequence frame receipt…", message);
    }
    void publish_status(std::string text, std::string& message) {
        status_ = message = std::move(text);
        if (editor_context)
            editor_context->record_status("Image export", status_);
    }
    void fail(const std::string& text, std::string& message) {
        if (frames_) {
            pending_ = confirm_ = false;
            frames_->error = text;
            if (frames_->finishing || (!frames_->owns_folder && !job_.valid())) {
                try {
                    release_frame_source();
                } catch (...) {
                }
                frames_.reset();
            }
            publish_status("Frame export failed: " + text, message);
        } else
            publish_status("Image save failed: " + text, message);
        report_error("view-image", status_);
    }
};
} // namespace forge::ui
