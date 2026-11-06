#pragma once
#include "../bounded_json.hpp"
#include "../native_io_path.hpp"
#include "../reflection_capture_source.hpp"
#include "reflection_capture_request.hpp"
#include "reflection_probe_view.hpp"
#include "source_import.hpp"
#include "view_image_work.hpp"
#include <algorithm>
#include <cctype>
#include <forge/render_scene.hpp>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <optional>
namespace forge::ui {
// Native implementation is injected by the presentation host. This boundary
// replaces only GPU delivery in portable editor tests, never authoring or IO.
struct ReflectionCaptureDriver {
    virtual ~ReflectionCaptureDriver() = default;
    virtual void step(Diligent::IDeviceContext*) = 0;
    virtual void cancel(const std::string&) = 0;
    virtual bool busy() const = 0;
    virtual unsigned completed_faces() const = 0;
    virtual std::string diagnostic() const = 0;
    virtual TextureData take() = 0;
};
class ReflectionCaptureTask final : public ViewImageWork {
  public:
    using Factory = std::function<std::unique_ptr<ReflectionCaptureDriver>(
        RenderScene, const ReflectionCaptureSettings&, EntityId)>;
    using Catalog = std::function<std::shared_ptr<const AssetCatalog>()>;
    ReflectionCaptureTask(Scene& scene, SceneDocument& document, SourceImport& source,
                          std::string entity, ReflectionCaptureRequest request, Factory factory,
                          Catalog catalog, std::function<std::string()> unavailable,
                          bool configure = false)
        : scene_(scene), document_(document), source_(source), entity_(std::move(entity)),
          request_(std::move(request)), factory_(std::move(factory)), catalog_(std::move(catalog)),
          unavailable_(std::move(unavailable)), project_(document.project()),
          scene_id_(scene.asset_id()), generation_(document.generation()),
          revision_(scene.revision()), configure_(configure) {
        document_.check_ownership();
        require_ready();
        (void)EntityId::parse(entity_);
        const auto effective = scene_.effective_document();
        std::optional<ReflectionProbeView> probe;
        for (const auto& row : effective.at("entities"))
            if (row.at("id") == entity_)
                probe = reflection_probe_view(row);
        if (!probe)
            throw AuthoringError("invalid_probe", "Choose a resolved authored Reflection Probe");
        request_.settings.position = probe->center;
        assigned_map_ = probe->probe.texture.id;
        snapshot_ = extract_render_scene(effective);
        input_catalog_ = catalog_();
        if (!input_catalog_ || input_catalog_->project_root() != project_)
            throw AuthoringError("not_ready", "Wait for this project's published asset catalog");
        const auto index = AssetCatalog::project_index(project_);
        asset_storage::ordinary(index);
        const auto bytes = asset_storage::read(index, max_asset_index_bytes);
        const auto text = bytes ? std::string_view(*bytes) : std::string_view{};
        AssetCatalog disk(project_);
        if (bytes)
            disk.restore(asset_detail::parse_bounded_json(std::as_bytes(std::span(text)),
                                                          max_asset_index_bytes));
        if (disk.document() != input_catalog_->document())
            throw AuthoringError("stale_catalog", "Wait for the current published asset catalog");
        catalog_digest_ = asset_detail::content_digest(std::as_bytes(std::span(text)));
        const auto authored = scene_.document().dump();
        source_digest_ = asset_detail::content_digest(
            std::as_bytes(std::span(authored.data(), authored.size())));
        folder_ = path_text(request_.destination);
        if (configure_) {
            for (unsigned i = 2; std::filesystem::exists(project_ / folder_) && i < 10000; ++i)
                folder_ = "Assets/ReflectionMap" + std::to_string(i);
            popup_ = true;
            status_ = "Choose reflection capture settings";
        } else
            start();
    }
    bool busy() const override { return !finished_; }
    Json state() const override {
        Json value{{"kind", "reflection"},
                   {"state", phase_},
                   {"status", status_},
                   {"error", error_},
                   {"source_commit_diagnostic", source_commit_diagnostic_},
                   {"probe", entity_},
                   {"scene_revision", revision_},
                   {"document_generation", generation_},
                   {"size", request_.settings.size},
                   {"position", request_.settings.position},
                   {"completed_faces", faces_},
                   {"destination", folder_},
                   {"assigned", assigned_},
                   {"update_assigned", request_.update_assigned},
                   {"cancel_requested", cancelled_}};
        if (batch_)
            value["import_batch"] = batch_;
        if (asset_)
            value["asset"] = asset_;
        return value;
    }
    void poll(Diligent::IDeviceContext* context) override {
        if (finished_)
            return;
        try {
            if (!batch_ && !cancelled_) {
                if (!current() || catalog_() != input_catalog_)
                    stop("Reflection capture cancelled because its Scene or catalog changed");
                else {
                    document_.check_ownership();
                    const auto reason = unavailable_ ? unavailable_() : std::string{};
                    if (!reason.empty())
                        stop(reason);
                }
            }
            if (driver_) {
                driver_->step(context);
                faces_ = driver_->completed_faces();
                if (driver_->busy()) {
                    if (!cancelled_)
                        status_ = "Capturing reflection faces: " + std::to_string(faces_) + " / 6";
                    return;
                }
                if (cancelled_) {
                    driver_.reset();
                    finish_cancel();
                    return;
                }
                if (const auto reason = driver_->diagnostic(); !reason.empty())
                    throw std::runtime_error(reason);
                auto cube = driver_->take();
                driver_.reset();
                phase_ = "encoding";
                status_ = "Encoding the captured HDR map";
                encoded_ = std::async(std::launch::async, [cube = std::move(cube)] {
                    TextureLimits limits;
                    limits.bytes = 64 * 1024 * 1024;
                    return encode_reflection_capture_source(cube, limits);
                });
            }
            if (encoded_.valid()) {
                if (encoded_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return;
                auto bytes = encoded_.get();
                if (cancelled_) {
                    finish_cancel();
                    return;
                }
                if (source_.busy())
                    throw std::runtime_error("Finish the current import before capturing a map");
                source_.generated_source(document_,
                                         request_.update_assigned
                                             ? revision_source_.filename()
                                             : std::filesystem::path("Reflection.dds"),
                                         std::move(bytes), description(), catalog_digest_,
                                         std::filesystem::u8path(folder_), reveal_,
                                         request_.update_assigned ? assigned_map_ : AssetId{});
                batch_ = source_.operation().at("batch").get<std::uint64_t>();
                phase_ = "import";
                status_ = "Review the generated reflection source before importing";
            }
            if (batch_)
                poll_import();
            else if (cancelled_ && !driver_ && !encoded_.valid())
                finish_cancel();
        } catch (const std::exception& e) {
            error_ = e.what();
            stop("Reflection capture failed: " + error_);
            if (driver_ && !driver_->busy())
                driver_.reset();
            if (!driver_ && !encoded_.valid()) {
                phase_ = "failed";
                finished_ = true;
            }
            report_error("reflection-capture", error_);
        }
    }
    bool cancel() override {
        if (finished_)
            return false;
        stop("Reflection capture cancelled; completed source copies and assets remain");
        if (batch_ && source_.operation().at("batch") == batch_)
            source_.automation_cancel(batch_);
        if (!batch_ && !driver_ && !encoded_.valid())
            finish_cancel();
        return true;
    }
    void draw() override {
        if (finished_ || batch_) {
            if (ImGui::IsPopupOpen("Capture reflection map") &&
                ImGui::BeginPopupModal("Capture reflection map")) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            return;
        }
        if (popup_)
            ImGui::OpenPopup("Capture reflection map");
        popup_ = false;
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * .5f,
                                 viewport->WorkPos.y + viewport->WorkSize.y * .5f},
                                ImGuiCond_Appearing, {.5f, .5f});
        ImGui::SetNextWindowSizeConstraints({0, 0},
                                            {viewport->WorkSize.x - 32, viewport->WorkSize.y - 32});
        if (!ImGui::BeginPopupModal("Capture reflection map", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::PushTextWrapPos(
            ImGui::GetCursorPosX() +
            std::min(560.f * ImGui::GetFontSize() / 13.f, viewport->WorkSize.x - 100));
        if (configure_) {
            ImGui::TextWrapped("Capture six HDR faces at the selected probe's world position.");
            ImGui::Text("Position: %.3f, %.3f, %.3f m", request_.settings.position[0],
                        request_.settings.position[1], request_.settings.position[2]);
            int size = static_cast<int>(request_.settings.size);
            ImGui::SetNextItemWidth(180);
            if (ImGui::InputInt("Face size (pixels)", &size))
                request_.settings.size = unsigned(std::max(size, 0));
            FORGE_UI_PROBE("reflection-capture:size");
            ImGui::SetNextItemWidth(180);
            ImGui::InputDouble("Near plane (m)", &request_.settings.near_plane, 0, 0, "%.6f");
            FORGE_UI_PROBE("reflection-capture:near");
            ImGui::SetNextItemWidth(180);
            ImGui::InputDouble("Far plane (m)", &request_.settings.far_plane, 0, 0, "%.3f");
            FORGE_UI_PROBE("reflection-capture:far");
            ImGui::SetNextItemWidth(180);
            ImGui::InputScalar("Layers", ImGuiDataType_U32, &request_.settings.layers);
            FORGE_UI_PROBE("reflection-capture:layers");
            ImGui::Checkbox("Update assigned map", &request_.update_assigned);
            FORGE_UI_PROBE("reflection-capture:update");
            if (request_.update_assigned)
                ImGui::TextWrapped("Rebake the assigned capture DDS source. Every consumer keeps "
                                   "the same Texture asset ID. Saved import settings are retained. "
                                   "Review the source replacement after capture; Scene Undo does "
                                   "not restore overwritten source bytes.");
            else {
                ImGui::SetNextItemWidth(std::min(420.f, viewport->WorkSize.x - 160));
                ImGui::InputText("New folder", &folder_);
                FORGE_UI_PROBE("reflection-capture:folder");
            }
            ImGui::Checkbox("Assign to this probe after import", &request_.assign);
            FORGE_UI_PROBE("reflection-capture:assign");
            ImGui::TextWrapped(
                "For a new map, choose a new folder below Assets with an existing parent. "
                "The import review opens after capture. Assignment resets map "
                "rotation to 0 degrees and is one Scene Undo step; save the Scene "
                "afterward. Undo does not remove generated files or assets.");
            ImGui::TextWrapped(
                "Static authored snapshot, source lighting and layer mask. "
                "Local/screen reflections and display effects are excluded. "
                "Runtime recapture is unavailable. Updating requires an independent imported "
                "capture DDS cube with unchanged source and settings.");
            std::string validation;
            try {
                validate();
            } catch (const std::exception& e) {
                validation = e.what();
            }
            if (!validation.empty())
                ImGui::TextWrapped("%s", validation.c_str());
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            ImGui::BeginDisabled(!validation.empty());
            if (button(request_.update_assigned ? "Rebake map" : "Capture map",
                       "Capture an HDR source, then review its copy or explicit source update.")) {
                try {
                    start();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            FORGE_UI_PROBE("reflection-capture:start");
            ImGui::EndDisabled();
        } else {
            ImGui::TextWrapped("%s", status_.c_str());
            ImGui::ProgressBar(float(faces_) / 6);
        }
        if (button("Cancel capture", "Retire accepted work; preserve any published asset.")) {
            cancel();
            ImGui::CloseCurrentPopup();
        }
        FORGE_UI_PROBE("reflection-capture:cancel");
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    void reveal(bool enabled) { reveal_ = enabled; }

  private:
    Scene& scene_;
    SceneDocument& document_;
    SourceImport& source_;
    std::string entity_;
    ReflectionCaptureRequest request_;
    Factory factory_;
    Catalog catalog_;
    std::function<std::string()> unavailable_;
    std::filesystem::path project_;
    AssetId scene_id_, asset_, assigned_map_;
    std::filesystem::path revision_source_;
    std::uint64_t generation_, revision_, batch_ = 0;
    bool configure_ = false, popup_ = false, reveal_ = true, cancelled_ = false, finished_ = false,
         assigned_ = false;
    unsigned faces_ = 0;
    std::string folder_, catalog_digest_, source_digest_, phase_ = "configure", status_, error_,
                                                          source_commit_diagnostic_;
    RenderScene snapshot_;
    std::shared_ptr<const AssetCatalog> input_catalog_;
    std::unique_ptr<ReflectionCaptureDriver> driver_;
    std::future<std::vector<std::byte>> encoded_;
    bool current() const {
        return document_.project() == project_ && document_.generation() == generation_ &&
               scene_.asset_id() == scene_id_ && scene_.revision() == revision_;
    }
    void require_ready() const {
        if (!factory_ || !catalog_)
            throw AuthoringError("unavailable", "Reflection capture is unavailable in this host");
        if (const auto reason = unavailable_ ? unavailable_() : std::string{}; !reason.empty())
            throw AuthoringError("capture_busy", reason);
    }
    void validate() const {
        validate_reflection_capture_settings(request_.settings);
        if (request_.update_assigned)
            (void)update_source();
        else
            (void)validate_source_copy_destination(project_, std::filesystem::u8path(folder_));
    }
    std::filesystem::path update_source() const {
        const auto at = input_catalog_->records().find(assigned_map_);
        if (!assigned_map_ || at == input_catalog_->records().end() || at->second.subasset ||
            at->second.type != TextureAsset::type)
            throw AuthoringError("invalid_map",
                                 "Assign an independent imported capture Texture first");
        const auto& record = at->second;
        const auto imported = record.metadata.find("forge.import");
        auto extension = path_utf8(record.source.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (imported == record.metadata.end() || !imported->is_object() ||
            imported->value("importer", std::string{}) != "forge.texture.container" ||
            extension != ".dds")
            throw AuthoringError("invalid_map", "Updating requires an imported capture DDS cube");
        const auto path = ProjectPaths(project_).resolve(record.source);
        asset_storage::ordinary(path);
        std::ifstream file(asset_detail::native_io_path(path), std::ios::binary);
        std::array<std::byte, 148> header;
        if (!file.read(reinterpret_cast<char*>(header.data()), header.size()))
            throw AuthoringError("invalid_map", "Cannot read the assigned capture DDS source");
        (void)reflection_capture_source_size(header);
        return record.source;
    }
    void start() {
        require_ready();
        if (!current() || catalog_() != input_catalog_)
            throw AuthoringError("stale_scene", "The capture Scene or catalog changed");
        validate();
        if (request_.update_assigned) {
            revision_source_ = update_source();
            folder_ = path_text(revision_source_.parent_path());
        }
        driver_ = factory_(snapshot_, request_.settings, EntityId::parse(entity_));
        if (!driver_)
            throw std::runtime_error("Reflection capture host returned no scoped job");
        snapshot_ = {};
        configure_ = false;
        error_.clear();
        phase_ = "capture";
        status_ = "Capturing reflection faces: 0 / 6";
    }
    Json description() const {
        return {{"format", "forge.reflection-capture"},
                {"version", 1},
                {"scene", scene_id_},
                {"probe", entity_},
                {"scene_revision", revision_},
                {"source_digest", source_digest_},
                {"catalog_digest", catalog_digest_},
                {"position", request_.settings.position},
                {"size", request_.settings.size},
                {"near_plane", request_.settings.near_plane},
                {"far_plane", request_.settings.far_plane},
                {"layers", request_.settings.layers},
                {"pixels", "RGBA16F linear HDR cube, +X -X +Y -Y +Z -Z"},
                {"assign_requested", request_.assign},
                {"update_assigned", request_.update_assigned},
                {"updated_asset", request_.update_assigned ? Json(assigned_map_) : Json()},
                {"map_rotation_degrees", 0}};
    }
    void stop(std::string reason) {
        cancelled_ = true;
        status_ = std::move(reason);
        if (driver_ && driver_->busy())
            driver_->cancel(status_);
    }
    void finish_cancel() {
        phase_ = error_.empty() ? "cancelled" : "failed";
        finished_ = true;
    }
    void poll_import() {
        const auto state = source_.operation();
        source_commit_diagnostic_ = state.value("source_commit_diagnostic", std::string{});
        if (state.at("batch") != batch_) {
            status_ = "Reflection import changed; assign any published map explicitly";
            phase_ = "unassigned";
            finished_ = true;
            return;
        }
        if (state.contains("destination"))
            folder_ = state.at("destination").get<std::string>();
        if (state.at("cancel_requested") == true)
            cancelled_ = true;
        if (cancelled_ && (state.at("state") == "review" || state.at("state") == "choose")) {
            finish_cancel();
            return;
        }
        if (source_.finished()) {
            for (const auto& result : state.at("results")) {
                if (result.at("published") == true)
                    asset_ = AssetId::parse(result.at("asset").get<std::string>());
                else if (!result.at("diagnostic").get<std::string>().empty())
                    error_ = result.at("diagnostic").get<std::string>();
            }
            if (asset_ && !cancelled_ && request_.assign && current() &&
                (!unavailable_ || unavailable_().empty())) {
                document_.check_ownership();
                apply_authoring(scene_,
                                Json::array({{{"operation", "property.set"},
                                              {"arguments",
                                               {{"entity", entity_},
                                                {"component", "forge.reflection_probe"},
                                                {"field", "texture"},
                                                {"value", AssetRef<TextureAsset>{asset_}}}}},
                                             {{"operation", "property.set"},
                                              {"arguments",
                                               {{"entity", entity_},
                                                {"component", "forge.reflection_probe"},
                                                {"field", "rotation_degrees"},
                                                {"value", 0.}}}}}),
                                revision_);
                assigned_ = true;
            }
            if (const auto diagnostic = state.value("diagnostic", std::string{});
                !diagnostic.empty())
                error_ = diagnostic;
            phase_ = asset_       ? assigned_                  ? "assigned"
                                    : request_.update_assigned ? "updated"
                                                               : "unassigned"
                     : cancelled_ ? "cancelled"
                     : error_.empty() && state.at("copied") == true ? "source_only"
                                                                    : "failed";
            status_ =
                assigned_ ? "Reflection map assigned. Save the Scene to keep this reference."
                : phase_ == "updated" ? "Reflection map updated. Existing Texture references "
                                        "are retained; probe values were not changed."
                : asset_ ? "Reflection map imported; assign it explicitly from Content."
                : phase_ == "source_only"
                    ? request_.update_assigned
                          ? "Reflection source updated without reimport in this batch. Closing "
                            "the review resumes normal automatic source updates."
                          : "Reflection source copied. Import it through Content before assignment."
                    : "Reflection source import ended without a published map.";
            if (!error_.empty())
                report_error("reflection-capture", error_);
            finished_ = true;
        } else if (state.at("open") == false || state.at("state") == "cancelled" ||
                   state.at("state") == "failed") {
            error_ = state.value("diagnostic", std::string{});
            if (error_.empty())
                cancelled_ = true;
            status_ = "Reflection import closed; the previous map assignment is unchanged";
            finish_cancel();
        } else if (state.at("state") != "review" && state.at("state") != "preparing")
            status_ = "Importing the captured reflection source";
    }
};
} // namespace forge::ui
