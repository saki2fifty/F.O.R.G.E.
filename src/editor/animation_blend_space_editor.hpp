#pragma once
#include "../animation_blend_space_authoring.hpp"
#include "../animation_blend_space_geometry.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <functional>
namespace forge {
class AnimationBlendSpaceEditor {
    friend struct AssetDocumentAutomation;

  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const AnimationBlendSpaceDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (can_undo()) {
            document_->undo();
            changed();
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
            open_error_ = true;
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button(
                "New animation blend space...",
                "Create compatible clips along one numeric axis or on a two-dimensional plane."))
            ImGui::OpenPopup("New animation blend space");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New animation blend space", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("animation_blend_space:new_path");
            const bool dimensions_open =
                ImGui::BeginCombo("Dimensions", new_dimensions_ ? "2D plane" : "1D axis");
            FORGE_UI_PROBE("animation_blend_space:new_dimensions");
            if (dimensions_open) {
                for (int axis = 0; axis < 2; ++axis) {
                    if (ImGui::Selectable(axis ? "2D plane" : "1D axis", new_dimensions_ == axis))
                        new_dimensions_ = axis;
                    FORGE_UI_PROBE("animation_blend_space:new_dimensions:" +
                                   std::to_string(axis + 1));
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create the source document; choose an imported skeleton and clips.")) {
                try {
                    auto created = AnimationBlendSpaceDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_), [&](AssetId id) {
                            return AnimationBlendSpaceSource::create(id,
                                                                     unsigned(new_dimensions_ + 1));
                        });
                    const auto source = created->locator();
                    created.reset();
                    load(project, source);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a file."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            finish_close();
            next_source_.reset();
        }
        if (service_)
            for (auto& result : service_->poll()) {
                if (result.job.id != job_)
                    continue;
                job_ = 0;
                if (result.published) {
                    published_ = std::make_shared<const AssetCatalog>(result.publication->catalog);
                    catalog_ = published_;
                    needs_publish_ = false;
                    error_.clear();
                    message = "Animation blend space saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument& project, Scene& scene, bool locked,
              const std::function<void()>& preview = {}) {
        (void)project;
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Animation Blend Space###Animation Blend Space", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close the failed source document."))
                        request_close();
                }
                ImGui::End();
            }
            return;
        }
        if (save_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool opened = true;
        const bool expanded =
            ImGui::Begin("Animation Blend Space###Animation Blend Space", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_blend_space",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped(
                "Place compatible clips on one axis or a two-axis plane. Neighboring clips "
                "share one cycle phase; positions beyond the boundary clamp.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button(
                    "Save / Publish",
                    "Save and cook. Failed publication retains the last working blend space."))
                request_save();
            FORGE_UI_PROBE("animation_blend_space:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo the blend-space source edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo the blend-space source edit."))
                redo();
            ImGui::EndDisabled();
            auto draft = document_->source().document;
            Json skeleton = draft.at("skeleton");
            if (asset_ref_picker(*catalog_, skeleton, SkeletonAsset::type, "Skeleton", false))
                mutate("Choose blend-space skeleton", [&](Json& d) { d["skeleton"] = skeleton; });
            double cycle = draft.at("cycle_seconds");
            if (ImGui::InputDouble("Cycle (seconds)", &cycle, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Set blend-space cycle", [&](Json& d) { d["cycle_seconds"] = cycle; });
            FORGE_UI_PROBE("animation_blend_space:cycle");
            ui::help("All clips share this cycle; their individual durations may differ. Press "
                     "Enter to commit.");
            const bool planar = draft.at("version") == 2;
            ImGui::SeparatorText(planar ? "Samples on the plane" : "Samples on the axis");
            if (planar)
                plane(draft, locked || pending());
            else {
                axis(draft);
                if (ui::button("Convert to 2D",
                               "Keep sample identities and X positions; begin with Y = 0. Undo "
                               "restores the one-dimensional draft."))
                    mutate("Convert blend space to 2D", [](Json& d) {
                        d["version"] = 2;
                        for (auto& sample : d["samples"])
                            sample["position"] = {{"x", sample.at("position")}, {"y", 0}};
                    });
                FORGE_UI_PROBE("animation_blend_space:convert_2d");
            }
            ImGui::BeginDisabled(draft.at("samples").size() >= 64);
            if (ui::button("Add sample",
                           "Add a clip point with a unique position; choose its clip below.")) {
                const auto id = AssetId::generate();
                mutate("Add blend-space sample", [&](Json& d) {
                    double position = 0;
                    while (std::any_of(
                        d["samples"].begin(), d["samples"].end(), [&](const auto& sample) {
                            const auto& p = sample.at("position");
                            return planar ? p.at("x").template get<double>() == position &&
                                                p.at("y").template get<double>() == 0
                                          : p.template get<double>() == position;
                        }))
                        ++position;
                    d["samples"].push_back(
                        {{"id", id},
                         {"clip", nullptr},
                         {"position", planar ? Json{{"x", position}, {"y", 0}} : Json(position)}});
                });
                selected_ = id;
            }
            FORGE_UI_PROBE("animation_blend_space:add_sample");
            ImGui::EndDisabled();
            if (ImGui::BeginChild("Blend samples", {0, 190}, ImGuiChildFlags_Borders)) {
                ImGuiListClipper clipper;
                clipper.Begin(int(draft.at("samples").size()),
                              ImGui::GetTextLineHeightWithSpacing());
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const auto& sample = draft.at("samples")[std::size_t(i)];
                        const auto id = sample.at("id").get<AssetId>();
                        const auto& point = sample.at("position");
                        auto label = planar
                                         ? std::to_string(point.at("x").get<double>()) + ", " +
                                               std::to_string(point.at("y").get<double>()) + " | "
                                         : std::to_string(point.get<double>()) + " | ";
                        if (sample.at("clip").is_null())
                            label += "Choose a clip";
                        else {
                            const auto found =
                                catalog_->records().find(sample.at("clip").get<AssetId>());
                            label += found == catalog_->records().end()
                                         ? "Missing clip"
                                         : asset_display(found->second);
                            if (found != catalog_->records().end() &&
                                !asset_picker_scope_matches(found->second, owner_))
                                label += " (different model)";
                        }
                        if (ImGui::Selectable((label + "##" + id.str()).c_str(), selected_ == id))
                            selected_ = id;
                        FORGE_UI_PROBE("animation_blend_space:sample:" + id.str());
                    }
            }
            ImGui::EndChild();
            for (const auto& sample : draft.at("samples"))
                if (selected_ && sample.at("id") == Json(selected_)) {
                    auto point = sample.at("position");
                    double position = planar ? point.at("x").get<double>() : point.get<double>();
                    if (ImGui::InputDouble(planar ? "Sample X" : "Sample position", &position, 0, 0,
                                           "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                        if (planar)
                            point["x"] = position;
                        else
                            point = position;
                        mutate_entry("Move blend-space sample", "position", point);
                    }
                    FORGE_UI_PROBE("animation_blend_space:position");
                    if (planar) {
                        double y = sample.at("position").at("y");
                        if (ImGui::InputDouble("Sample Y", &y, 0, 0, "%.3f",
                                               ImGuiInputTextFlags_EnterReturnsTrue)) {
                            point = sample.at("position");
                            point["y"] = y;
                            mutate_entry("Move blend-space sample", "position", point);
                        }
                        FORGE_UI_PROBE("animation_blend_space:position_y");
                    }
                    auto clip = sample.at("clip");
                    if (asset_ref_picker(*catalog_, clip, AnimationClipAsset::type, "Sample clip",
                                         false, owner_))
                        mutate_entry("Choose blend-space clip", "clip", clip);
                    if (ui::button(
                            "Remove sample",
                            "Remove this clip point; neighboring samples fill the interval.")) {
                        mutate("Remove blend-space sample", [&](Json& d) {
                            std::erase_if(
                                d["samples"].get_ref<Json::array_t&>(),
                                [&](const auto& e) { return e.at("id") == Json(selected_); });
                        });
                        selected_ = {};
                    }
                    FORGE_UI_PROBE("animation_blend_space:remove_sample");
                    break;
                }
            ImGui::SeparatorText("Scene object");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty());
            if (ui::button(
                    "Attach to selected Animator",
                    "Assign this blend space and clear incompatible modes in one Scene Undo step."))
                attach(scene);
            FORGE_UI_PROBE("animation_blend_space:attach");
            ImGui::SameLine();
            if (preview &&
                ui::button("Open Animation Preview", "Preview the selected object's coordinates "
                                                     "without changing its authored settings."))
                preview();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing animation blend space...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending animation blend space changes");
        if (ImGui::BeginPopupModal("Pending animation blend space changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save and publish, discard the draft, or keep editing?");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Publish before closing.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::button("Discard", "Close without saving this draft.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Keep editing", "Cancel closing.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    std::unique_ptr<AnimationBlendSpaceDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_source_;
    std::optional<AssetId> owner_;
    AssetJobId job_ = 0;
    AssetId selected_;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    char path_[256] = "Assets/Motion.blendspace.json";
    std::string error_;
    int new_dimensions_ = 0;
    std::uint64_t plane_revision_ = 0;
    std::optional<AnimationBlendSpace> plane_layout_;
    std::string plane_error_;
    AssetId dragged_sample_;
    std::uint64_t drag_revision_ = 0;
    Json drag_position_;
    bool drag_moved_ = false;
    void plane(const Json& draft, bool locked);
    void axis(const Json& draft) {
        const auto count = draft.at("samples").size();
        if (!count)
            return;
        double minimum = 1000000, maximum = -1000000;
        for (const auto& sample : draft.at("samples")) {
            minimum = std::min(minimum, sample.at("position").get<double>());
            maximum = std::max(maximum, sample.at("position").get<double>());
        }
        const auto at = ImGui::GetCursorScreenPos();
        const float width = std::max(1.f, ImGui::GetContentRegionAvail().x - 24);
        auto* draw = ImGui::GetWindowDrawList();
        const float y = at.y + 22;
        draw->AddLine({at.x + 12, y}, {at.x + 12 + width, y}, IM_COL32(95, 150, 180, 255), 2);
        for (const auto& sample : draft.at("samples")) {
            const double position = sample.at("position");
            const float fraction =
                maximum > minimum ? float((position - minimum) / (maximum - minimum)) : .5f;
            const float x = at.x + 12 + width * fraction;
            const bool selected = selected_ && sample.at("id") == Json(selected_);
            draw->AddCircleFilled({x, y}, selected ? 6 : 4,
                                  selected ? IM_COL32(245, 190, 60, 255)
                                           : IM_COL32(120, 210, 240, 255));
        }
        ImGui::Dummy({width + 24, 42});
        ImGui::Text("Axis: %.3f to %.3f", minimum, maximum);
    }
    void finish_close() {
        plane_revision_ = 0;
        plane_layout_.reset();
        dragged_sample_ = {};
        document_.reset();
        service_.reset();
        catalog_.reset();
        owner_ = AssetId{};
        job_ = 0;
        selected_ = {};
        needs_publish_ = save_ = close_ = open_error_ = false;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        plane_revision_ = 0;
        plane_layout_.reset();
        dragged_sample_ = {};
        auto doc = std::make_unique<AnimationBlendSpaceDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(project.writer_guard(),
                                                            animation_blend_space_import_registry(),
                                                            desktop_texture_target());
        const auto at = catalog->records().find(doc->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    animation_blend_space_byte_limit);
        const bool needs = at == catalog->records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(doc);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        error_.clear();
        refresh_owner();
        if (!document_->source().document.at("samples").empty())
            selected_ = document_->source().document.at("samples").front().at("id").get<AssetId>();
    }
    void refresh_owner() {
        owner_ = AssetId{};
        const auto& skeleton = document_->source().document.at("skeleton");
        if (skeleton.is_null())
            return;
        try {
            const auto id = skeleton.get<AssetId>();
            const auto resolved = catalog_->resolve(id, SkeletonAsset::type);
            if (resolved.state != AssetState::Available || !resolved.record->subasset)
                throw std::runtime_error("Choose a current skeleton from an imported model");
            owner_ = resolved.record->subasset->owner;
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void changed() {
        needs_publish_ = true;
        refresh_owner();
    }
    template <class F> void mutate(std::string label, F edit) {
        try {
            document_->edit(document_->revision(), std::move(label), edit);
            error_.clear();
            changed();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void mutate_entry(std::string label, const char* field, const Json& value) {
        mutate(std::move(label), [&](Json& d) {
            for (auto& row : d["samples"])
                if (row.at("id") == Json(selected_))
                    row[field] = value;
        });
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& c, const auto& p, const auto&) {
                    prepare_animation_blend_space_publication(c, p);
                },
                [](const auto&, const auto&) {});
        } catch (const std::exception& e) {
            needs_publish_ = true;
            close_ = false;
            error_ = e.what();
        }
    }
    void attach(Scene& scene) {
        try {
            if (!ui::editor_context)
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto id = ui::editor_context->selection.entity();
            auto actor = scene.entity(id);
            if (!actor || !actor.has<Animator>())
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto& config = actor.get<Animator>();
            if (config.skeleton.id &&
                Json(config.skeleton.id) != document_->source().document.at("skeleton"))
                throw std::runtime_error("Choose an Animator with this imported skeleton");
            const auto compiled = document_->source().compiled();
            Json values{{"blend_weight", 0},
                        {"blend_root", nullptr},
                        {"blend_mask", nullptr},
                        {"blend_mode", 0},
                        {"blend_clip", nullptr},
                        {"states", nullptr},
                        {"state", 0},
                        {"clip", nullptr},
                        {"skeleton", compiled.skeleton},
                        {"blend_space", compiled.asset},
                        {"blend_position", float(compiled.samples.front().position)},
                        {"blend_position_y", float(compiled.samples.front().position_y)}};
            auto commands = Json::array();
            for (const char* field :
                 {"blend_weight", "blend_root", "blend_mask", "blend_mode", "blend_clip", "states",
                  "state", "clip", "skeleton", "blend_space", "blend_position", "blend_position_y"})
                commands.push_back({{"operation", "property.set"},
                                    {"arguments",
                                     {{"entity", id},
                                      {"component", "forge.animator"},
                                      {"field", field},
                                      {"value", values.at(field)}}}});
            apply_authoring(scene, commands, scene.revision());
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
inline void AnimationBlendSpaceEditor::plane(const Json& draft, bool locked) {
    if (plane_revision_ != document_->revision()) {
        plane_revision_ = document_->revision();
        plane_layout_.emplace();
        auto& layout = *plane_layout_;
        layout.dimensions = 2;
        for (const auto& sample : draft.at("samples"))
            layout.samples.push_back({sample.at("id").get<AssetId>(),
                                      {},
                                      sample.at("position").at("x").get<double>(),
                                      sample.at("position").at("y").get<double>()});
        std::sort(layout.samples.begin(), layout.samples.end(), [](const auto& a, const auto& b) {
            return a.position == b.position ? a.position_y < b.position_y : a.position < b.position;
        });
        plane_error_.clear();
        try {
            if (!layout.samples.empty())
                animation_detail::prepare_blend_geometry(layout);
        } catch (const std::exception& e) {
            plane_error_ = e.what();
            layout.triangles.clear();
            layout.boundary.clear();
        }
    }
    const auto& layout = *plane_layout_;
    const float unit = ImGui::GetFrameHeight();
    const auto start = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), unit * 11};
    ImGui::InvisibleButton("Blend plane", size);
    FORGE_UI_PROBE("animation_blend_space:plane");
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float side = std::max(1.f, std::min(size.x, size.y) - unit * 2);
    const ImVec2 lower{start.x + (size.x - side) * .5f, start.y + (size.y + side) * .5f};
    const double extent = layout.samples.size() > 1 ? layout.extent : 1;
    const double x0 =
        (layout.samples.size() == 1 ? layout.samples[0].position - .5 : layout.origin[0]) -
        extent * .15;
    const double y0 =
        (layout.samples.size() == 1 ? layout.samples[0].position_y - .5 : layout.origin[1]) -
        extent * .15;
    const double span = extent * 1.3;
    const auto screen = [&](double x, double y) {
        return ImVec2{lower.x + float((x - x0) / span) * side,
                      lower.y - float((y - y0) / span) * side};
    };
    const auto mouse = ImGui::GetIO().MousePos;
    std::optional<std::size_t> nearest;
    float best = unit * unit * .36f;
    for (std::size_t i = 0; i < layout.samples.size(); ++i) {
        const auto p = screen(layout.samples[i].position, layout.samples[i].position_y);
        const float dx = mouse.x - p.x, dy = mouse.y - p.y, distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }
    if (locked || drag_revision_ != document_->revision())
        dragged_sample_ = {};
    if (!locked && ImGui::IsItemClicked() && nearest) {
        selected_ = dragged_sample_ = layout.samples[*nearest].id;
        drag_revision_ = document_->revision();
        drag_moved_ = false;
        for (const auto& sample : draft.at("samples"))
            if (sample.at("id") == Json(dragged_sample_))
                drag_position_ = sample.at("position");
    }
    if (dragged_sample_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (!active && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
            dragged_sample_ = {};
        else if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, unit * .15f)) {
            drag_moved_ = true;
            drag_position_["x"] =
                std::clamp(x0 + std::clamp(double((mouse.x - lower.x) / side), 0., 1.) * span,
                           -1000000., 1000000.);
            drag_position_["y"] =
                std::clamp(y0 + std::clamp(double((lower.y - mouse.y) / side), 0., 1.) * span,
                           -1000000., 1000000.);
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (drag_moved_ && selected_ == dragged_sample_)
                mutate_entry("Move blend-space sample", "position", drag_position_);
            dragged_sample_ = {};
        }
    }
    const auto point = [&](std::size_t i) {
        const auto& sample = layout.samples[i];
        return dragged_sample_ == sample.id ? screen(drag_position_.at("x"), drag_position_.at("y"))
                                            : screen(sample.position, sample.position_y);
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(start, {start.x + size.x, start.y + size.y}, true);
    draw->AddRectFilled(start, {start.x + size.x, start.y + size.y}, IM_COL32(24, 32, 40, 255));
    for (unsigned i = 0; i <= 4; ++i) {
        const float at = side * float(i) / 4;
        draw->AddLine({lower.x + at, lower.y}, {lower.x + at, lower.y - side},
                      IM_COL32(60, 77, 90, 255));
        draw->AddLine({lower.x, lower.y - at}, {lower.x + side, lower.y - at},
                      IM_COL32(60, 77, 90, 255));
    }
    for (const auto& triangle : layout.triangles)
        for (unsigned i = 0; i < 3; ++i)
            draw->AddLine(point(triangle[i]), point(triangle[(i + 1) % 3]),
                          IM_COL32(85, 145, 175, 255));
    if (layout.triangles.empty())
        for (const auto& edge : layout.boundary)
            draw->AddLine(point(edge[0]), point(edge[1]), IM_COL32(85, 145, 175, 255));
    for (std::size_t i = 0; i < layout.samples.size(); ++i) {
        const auto p = point(i);
        draw->AddCircleFilled(p, unit * .22f,
                              selected_ == layout.samples[i].id ? IM_COL32(245, 190, 60, 255)
                                                                : IM_COL32(120, 210, 240, 255));
#ifdef FORGE_UI_FIXTURE
        const ImVec2 minimum{p.x - unit * .22f, p.y - unit * .22f};
        const ImVec2 maximum{p.x + unit * .22f, p.y + unit * .22f};
        FORGE_UI_RECT_PROBE("animation_blend_space:plane_point:" + layout.samples[i].id.str(),
                            minimum, maximum, !locked);
#endif
    }
    draw->AddText({lower.x + side, lower.y}, IM_COL32(185, 200, 210, 255), "X");
    draw->AddText({lower.x, lower.y - side - unit * .75f}, IM_COL32(185, 200, 210, 255), "Y");
    draw->PopClipRect();
    if (hovered && nearest && !dragged_sample_)
        ImGui::SetTooltip("X %.3f, Y %.3f\nClick to select; drag to move.",
                          layout.samples[*nearest].position, layout.samples[*nearest].position_y);
    ui::help("Select a point or a list row. Drag a point, or enter its X and Y below. Escape "
             "cancels a drag.");
    if (!plane_error_.empty())
        ui::field_error(plane_error_);
}
} // namespace forge
