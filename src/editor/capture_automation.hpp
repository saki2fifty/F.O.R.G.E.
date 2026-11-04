#pragma once
#include "../automation_image.hpp"
#include "../texture_image.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "reflection_capture_request.hpp"
#include "view_image.hpp"
namespace forge {
struct CaptureAutomationProviders {
    std::function<void(ui::ViewImageTask::View)> show;
    std::function<void(const std::string&)> open_sequence;
    std::function<ui::SequenceFrameSource&()> sequence;
    std::function<std::string(bool)> unavailable;
    std::function<void(const std::string&, const ui::ReflectionCaptureRequest&)> reflection;
    std::function<ui::ViewImageTask::PixelInput(AssetRef<TextureAsset>,
                                                std::optional<TextureSemantic>, TextureImageSlice)>
        texture;
};
struct CaptureAutomation {
    static Json state(ui::ViewImageTask& owner, EditorFiles& files) {
        files.document.check_ownership();
        if (owner.project_ != files.document.project())
            return {{"started", false}, {"busy", false}};
        Json value{
            {"started", true},
            {"busy", owner.busy()},
            {"operation", owner.operation_},
            {"artifact_operation", owner.artifact_operation_},
            {"status", owner.status_},
            {"error", owner.error_},
            {"preview_ready", owner.preview_ && owner.preview_operation_ == owner.operation_},
            {"preview_error", owner.preview_error_},
            {"view", owner.view_ == ui::ViewImageTask::View::Scene ? "scene" : "game"}};
        if (owner.scene_)
            value["scene"] = owner.scene_;
        if (owner.view_ == ui::ViewImageTask::View::Texture && owner.input_) {
            value["view"] = "texture";
            value["texture"] = owner.input_->source;
        }
        if (owner.work_) {
            value["view"] = "reflection";
            value["reflection"] = owner.work_->state();
        }
        if (!owner.destination_.empty() &&
            view_image_format(owner.destination_) == ViewImageFormat::Jpeg)
            value["jpeg_quality"] = owner.encoding_.jpeg_quality;
        if (!owner.completed_image_.empty()) {
            value["file"] = path_utf8(owner.completed_image_);
            value["file_mime"] = view_image_mime(view_image_format(owner.completed_image_));
            if (owner.completed_component_bits_)
                value["component_bits"] = owner.completed_component_bits_;
            if (owner.completed_component_bits_ && value.contains("texture"))
                value["texture"]["profile"] =
                    owner.completed_component_bits_ == 16 ? "stored_rgba16" : "stored_rgba8";
        }
        const auto* frames =
            owner.frames_ ? static_cast<const ui::ViewImageTask::FrameReceipt*>(&*owner.frames_)
            : owner.completed_frames_ ? &*owner.completed_frames_
                                      : nullptr;
        if (frames)
            value["sequence"] = {{"folder", path_utf8(frames->folder)},
                                 {"planned_frames", frames->plan.frame_count},
                                 {"completed_frames", frames->completed},
                                 {"width", frames->width},
                                 {"height", frames->height},
                                 {"state", frames->state},
                                 {"error", frames->error},
                                 {"reason", frames->reason}};
        return value;
    }
    static void ready(EditorFiles& files, const CaptureAutomationProviders& providers,
                      bool sequence) {
        files.document.check_ownership();
        if (providers.unavailable) {
            const auto reason = providers.unavailable(sequence);
            if (!reason.empty())
                throw AuthoringError("capture_busy", reason);
        }
    }
    static std::filesystem::path destination(const std::filesystem::path& root,
                                             const Json& request) {
        const ProjectPaths output(root);
        if (output.root() != root)
            throw AuthoringError("invalid_path", "The output grant folder changed");
        const auto relative = automation_relative_path(request.at("path").get<std::string>());
        asset_storage::ordinary(output.root() / relative);
        const auto resolved = output.resolve(relative);
        if (resolved == root || !std::filesystem::is_directory(resolved.parent_path()))
            throw AuthoringError("invalid_path", "Choose a named output with an existing parent");
        return resolved;
    }
    static Json sequence_state(const CaptureAutomationProviders& providers, Scene& scene,
                               EditorFiles& files) {
        auto& source = providers.sequence();
        if (!source.frame_export_ready())
            return {{"ready", false}};
        const auto info = source.frame_export_info();
        if (!info.catalog || info.catalog->project_root() != files.document.project() ||
            info.controller.scene != scene.asset_id())
            return {{"ready", false}};
        return {{"ready", true},
                {"controller", info.controller.entity},
                {"scene", info.controller.scene},
                {"duration", info.duration},
                {"source_revision", info.revision},
                {"preview_generation", info.preview_generation}};
    }
    static std::filesystem::path image_destination(const std::filesystem::path& root,
                                                   const Json& request, bool hdr = false) {
        const auto file = destination(root, request);
        try {
            if (view_image_format(file) == ViewImageFormat::Hdr && !hdr)
                throw std::runtime_error(
                    "Rendered views support PNG, lossless WebP or opaque JPEG output");
        } catch (const std::exception& e) {
            throw AuthoringError("invalid_path", e.what());
        }
        if (std::filesystem::exists(file) && !request.value("replace", false))
            throw AuthoringError("output_exists", "Explicitly request replacement of this image");
        return file;
    }
    static ImageEncodingOptions encoding_options(const std::filesystem::path& file,
                                                 const Json& request) {
        if (request.contains("jpeg_quality") && view_image_format(file) != ViewImageFormat::Jpeg)
            throw AuthoringError("invalid_arguments", "jpeg_quality requires a JPEG destination");
        ImageEncodingOptions result{request.value("jpeg_quality", FORGE_JPEG_DEFAULT_QUALITY)};
        try {
            result.validate();
        } catch (const std::exception& e) {
            throw AuthoringError("invalid_arguments", e.what());
        }
        return result;
    }
    static std::vector<LiveAuthoringMethod> methods(ui::ViewImageTask& owner, Scene& scene,
                                                    EditorFiles& files,
                                                    CaptureAutomationProviders providers,
                                                    std::string& message,
                                                    const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "capture.status",
            "Read the actual view-image task and "
            "its PNG/WebP/sequence/reflection progress. Rendering and IO remain asynchronous and "
            "owned by the editor.",
            "capture.read", automation_object(), false,
            [&owner, &files](const Json&) { return state(owner, files); }));
        if (providers.sequence)
            result.push_back(
                automation_method("capture.sequence.read",
                                  "Read the existing Timeline "
                                  "sequence export source, cursor generation, duration "
                                  "and readiness. No rendering starts.",
                                  "capture.read", automation_object(), false,
                                  [&scene, &files, providers](const Json&) {
                                      files.document.check_ownership();
                                      return sequence_state(providers, scene, files);
                                  }));
        if (permissions.assets && providers.reflection)
            result.push_back(automation_method(
                "capture.reflection.start",
                "Capture a new linear HDR cube at an authored Reflection Probe. Edit-only; "
                "16–1024 pixels per face. The destination is a NEW folder below Assets with an "
                "existing parent. Rendering creates no requested source folder or published "
                "assets. Poll capture.status for "
                "the import_batch, then use asset.import.review/commit/cancel/close. Successful "
                "import assigns the new map and resets map yaw to zero in one Scene Undo step "
                "only if the original Scene/document revision remains current; otherwise the "
                "map remains available for explicit assignment. Save the Scene afterward. "
                "No runtime recapture, recursive reflections or display effects.",
                "asset.write",
                automation_object({{"probe", automation_uuid()},
                                   {"destination", automation_text(2047, 1)},
                                   {"size", automation_integer(16, 1024)},
                                   {"near_plane", {{"type", "number"}, {"exclusiveMinimum", 0}}},
                                   {"far_plane", {{"type", "number"}, {"exclusiveMinimum", 0}}},
                                   {"layers", automation_integer(0, UINT32_MAX)},
                                   {"assign", {{"type", "boolean"}}}},
                                  {"probe", "destination"}),
                true, [&owner, &files, providers](const Json& request) {
                    ready(files, providers, true);
                    if (owner.busy())
                        throw AuthoringError("capture_busy", "Wait for the active image task");
                    ui::ReflectionCaptureRequest settings;
                    settings.destination =
                        automation_relative_path(request.at("destination").get<std::string>());
                    settings.settings.size = request.value("size", 256u);
                    settings.settings.near_plane = request.value("near_plane", .1);
                    settings.settings.far_plane = request.value("far_plane", 100.);
                    settings.settings.layers = request.value("layers", UINT32_MAX);
                    settings.assign = request.value("assign", true);
                    validate_reflection_capture_settings(settings.settings);
                    providers.reflection(request.at("probe").get<std::string>(), settings);
                    return state(owner, files);
                }));
        const bool output_access = permissions.export_game && !permissions.export_root.empty();
        if (permissions.assets || output_access)
            result.push_back(automation_method(
                "capture.cancel",
                "Cancel this exact pending image, sequence or reflection task. Accepted work "
                "retires through its owner. Completed files/assets remain; reflection cancellation "
                "prevents automatic map assignment.",
                "capture.write",
                automation_object({{"operation", automation_uuid()}}, {"operation"}), true,
                [&owner, &files, &message, asset_write = permissions.assets,
                 output_write = output_access](const Json& request) {
                    files.document.check_ownership();
                    if (owner.project_ != files.document.project() ||
                        request.at("operation") != owner.operation_ || !owner.busy())
                        throw AuthoringError("wrong_job",
                                             "Choose the current project's exact image task");
                    if (owner.work_ ? !asset_write : !output_write)
                        throw AuthoringError(
                            "capability_denied",
                            "This capture needs its project-asset or output grant");
                    if (!owner.cancel(message))
                        throw AuthoringError("already_committing",
                                             "Wait for the committing readback/file task");
                    return state(owner, files);
                },
                true));
        if (!output_access)
            return result;
        const auto root = std::filesystem::canonical(permissions.export_root);
        result.push_back(automation_method(
            "output.folder.create",
            "Create one new folder inside "
            "the approved output root, with an existing parent. This does not adopt/delete "
            "existing files or change project content.",
            "capture.write", automation_object({{"path", automation_text(2047, 1)}}, {"path"}),
            true,
            [&files, providers, root](const Json& request) {
                ready(files, providers, false);
                const auto folder = destination(root, request);
                if (!std::filesystem::create_directory(folder))
                    throw AuthoringError("output_exists", "This output folder already exists");
                asset_storage::sync_directory(folder.parent_path());
                return Json{{"path", request.at("path")}, {"created", true}};
            },
            true));
        result.push_back(automation_method(
            "capture.image",
            "Read this task's exact ready PNG "
            "preview as an MCP image block. The bounded preview fits1024 pixels; full PNG output "
            "keeps its original dimensions. No arbitrary filesystem image path is accepted.",
            "capture.read", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            false,
            [&owner, &files, root](const Json& request) {
                files.document.check_ownership();
                if (owner.project_ != files.document.project() ||
                    request.at("operation") != owner.operation_ ||
                    owner.preview_operation_ != owner.operation_ || !owner.preview_)
                    throw AuthoringError(
                        "not_ready", "Read capture.status and wait for this operation's preview");
                const ProjectPaths output(root);
                if (output.root() != root)
                    throw AuthoringError("invalid_path", "The output grant changed");
                const auto relative = output.relative(owner.preview_file_);
                const auto& preview = *owner.preview_;
                return Json{
                    {"operation", owner.operation_},
                    {"file", path_utf8(relative)},
                    {"source_width", preview.source_width},
                    {"source_height", preview.source_height},
                    {"width", preview.width},
                    {"height", preview.height},
                    {"image",
                     {{"mimeType", "image/png"}, {"data", automation_image_base64(preview.png)}}}};
            },
            false, "image/png"));
        result.push_back(automation_method(
            "capture.start",
            "Capture the next rendered Scene or "
            "Game view through the existing image task. Use .png, lossless .webp or opaque sRGB "
            "JPEG (.jpg/.jpeg); jpeg_quality is1–100, default90, JPEG only. JPEG previews show "
            "decoded output. Save inside the "
            "approved "
            "folder. Existing files require replace:true. Poll status/image; this does not freeze "
            "the game or claim that a starting game already rendered a frame.",
            "capture.write",
            automation_object({{"view", {{"type", "string"}, {"enum", {"scene", "game"}}}},
                               {"path", automation_text(2047, 1)},
                               {"jpeg_quality",
                                automation_integer(FORGE_JPEG_QUALITY_MIN, FORGE_JPEG_QUALITY_MAX)},
                               {"replace", {{"type", "boolean"}}}},
                              {"view", "path"}),
            true,
            [&owner, &scene, &files, providers, root, &message](const Json& request) {
                ready(files, providers, false);
                const auto file = image_destination(root, request);
                const auto encoding = encoding_options(file, request);
                const auto view = request.at("view") == "scene" ? ui::ViewImageTask::View::Scene
                                                                : ui::ViewImageTask::View::Game;
                if (!owner.selected_path(view, file, files.document.project(), scene.asset_id(),
                                         message, {}, {}, encoding))
                    throw AuthoringError("capture_busy", owner.status());
                owner.preview_requested_ = true;
                providers.show(view);
                return state(owner, files);
            },
            true));
        if (providers.texture)
            result.push_back(automation_method(
                "capture.texture",
                "Export one published Texture mip/layer/face/depth slice inside the output root. "
                "PNG/sRGB lossless WebP retain R8/RG8/RGBA8 stored bytes (128 MiB source,64 MiB "
                "RGBA); missing G/B=0,A=255. PNG16 retains R16/RG16/RGBA16 samples without an "
                "sRGB label (256 MiB source,128 MiB RGBA16,136 MiB encoded); missingA=65535. "
                "Its PNG inspection preview quantizes to8-bit and component_bits reports16. "
                "JPEG (.jpg/.jpeg) requires opaque sRGB bytes; "
                "jpeg_quality is1–100, default90, JPEG only. JPEG is lossy and its preview shows "
                "decoded output. HDR (.hdr) accepts HdrColor RGBA16/32Float, finite "
                "nonnegative RGB and actual alpha1 (256 MiB source,192 MiB RGB float). RGBE "
                "quantizes RGB without tone mapping; HDR has no alpha or inline image preview. "
                "All profiles limit each axis to4096; other profiles cap encoded output at68 MiB. "
                "No source "
                "decode, import metadata or Scene Undo. Existing files require replace:true. "
                "Poll capture.status for the written file path/MIME; PNG/WebP/JPEG also support "
                "capture.image.",
                "capture.write",
                automation_object({{"asset", automation_uuid()},
                                   {"semantic",
                                    {{"type", "string"},
                                     {"enum", {"automatic", "color", "data", "normal", "hdr"}}}},
                                   {"mip", automation_integer(0, 31)},
                                   {"layer", automation_integer(0, 2047)},
                                   {"face", automation_integer(0, 5)},
                                   {"depth", automation_integer(0, 2047)},
                                   {"path", automation_text(2047, 1)},
                                   {"jpeg_quality", automation_integer(FORGE_JPEG_QUALITY_MIN,
                                                                       FORGE_JPEG_QUALITY_MAX)},
                                   {"replace", {{"type", "boolean"}}}},
                                  {"asset", "path"}),
                true,
                [&owner, &files, providers, root, &message](const Json& request) {
                    ready(files, providers, false);
                    const auto file = image_destination(root, request, true);
                    const auto encoding = encoding_options(file, request);
                    const auto usage = request.value("semantic", std::string("automatic"));
                    std::optional<TextureSemantic> semantic;
                    if (usage == "color")
                        semantic = TextureSemantic::Color;
                    else if (usage == "data")
                        semantic = TextureSemantic::Data;
                    else if (usage == "normal")
                        semantic = TextureSemantic::Normal;
                    else if (usage == "hdr")
                        semantic = TextureSemantic::HdrColor;
                    else if (usage != "automatic")
                        throw AuthoringError("invalid_arguments",
                                             "Choose an available Texture semantic");
                    const TextureImageSlice slice{
                        request.value("mip", 0u), request.value("layer", 0u),
                        request.value("face", 0u), request.value("depth", 0u)};
                    auto input =
                        providers.texture({request.at("asset").get<AssetId>()}, semantic, slice);
                    if (!owner.selected_path(ui::ViewImageTask::View::Texture, file,
                                             files.document.project(), {}, message, {},
                                             std::move(input), encoding))
                        throw AuthoringError("capture_busy", owner.status());
                    owner.preview_requested_ = true;
                    return state(owner, files);
                },
                true));
        result.push_back(automation_method(
            "capture.preview",
            "Prepare a preview of this task's "
            "completed PNG/WebP/JPEG, or an existing completed sequence frame. This reads only the "
            "owned "
            "artifact; poll status then capture.image using the returned new operation.",
            "capture.write",
            automation_object(
                {{"artifact_operation", automation_uuid()},
                 {"frame", automation_integer(0, ui::SequenceFramePlan::maximum_frames - 1)}},
                {"artifact_operation"}),
            true,
            [&owner, &files, providers, root, &message](const Json& request) {
                ready(files, providers, false);
                if (owner.project_ != files.document.project())
                    throw AuthoringError("wrong_job", "Choose this project's image task");
                const ProjectPaths output(root);
                if (output.root() != root)
                    throw AuthoringError("invalid_path", "The output grant changed");
                std::optional<unsigned> frame;
                if (request.contains("frame"))
                    frame = request.at("frame").get<unsigned>();
                const auto path = frame && owner.completed_frames_
                                      ? owner.completed_frames_->folder /
                                            owner.completed_frames_->plan.filename(*frame)
                                      : owner.completed_image_;
                if (path.empty())
                    throw AuthoringError("not_ready", "Choose an existing owned image/frame");
                (void)output.relative(path);
                if (!owner.begin_preview(request.at("artifact_operation"), frame, message))
                    throw AuthoringError("capture_busy", "Wait for the active image task");
                return state(owner, files);
            },
            true));
        append_sequence(result, owner, scene, files, providers, root, message);
        return result;
    }
    static void append_sequence(std::vector<LiveAuthoringMethod>& result, ui::ViewImageTask& owner,
                                Scene& scene, EditorFiles& files,
                                CaptureAutomationProviders providers,
                                const std::filesystem::path& root, std::string& message) {
        result.push_back(automation_method(
            "capture.sequence.open",
            "Open an authored Animation "
            "Sequence controller in the existing Timeline preview. Settle Timeline/source drafts "
            "first; this does not change the authored controller or its playback values.",
            "capture.write", automation_object({{"controller", automation_uuid()}}, {"controller"}),
            true, [&scene, &files, providers](const Json& request) {
                ready(files, providers, true);
                providers.open_sequence(request.at("controller"));
                return sequence_state(providers, scene, files);
            }));
        result.push_back(automation_method(
            "capture.sequence.start",
            "Export sampled Timeline "
            "preview frames through the existing image task at its exact source "
            "revision/generation. "
            "Choose a new folder inside the output grant. Samples are end-exclusive; fixed output "
            "uses real renderer pixels, not resized PNG files. Poll capture.status for "
            "frames/receipt.",
            "capture.write",
            automation_object(
                {{"path", automation_text(2047, 1)},
                 {"controller", automation_uuid()},
                 {"source_revision", automation_integer()},
                 {"preview_generation", automation_integer()},
                 {"start_seconds", {{"type", "number"}, {"minimum", 0}}},
                 {"frames_per_second", {{"type", "number"}, {"minimum", 0}, {"maximum", 240}}},
                 {"frame_count", automation_integer(1, ui::SequenceFramePlan::maximum_frames)},
                 {"fixed_output", {{"type", "boolean"}}},
                 {"width", automation_integer(1, 4096)},
                 {"height", automation_integer(1, 4096)}},
                {"path", "controller", "source_revision", "preview_generation", "start_seconds",
                 "frames_per_second", "frame_count"}),
            true, [&owner, &scene, &files, providers, root, &message](const Json& request) {
                ready(files, providers, true);
                const auto observed = sequence_state(providers, scene, files);
                if (!observed.at("ready").get<bool>() ||
                    observed.at("controller") != request.at("controller") ||
                    observed.at("source_revision") != request.at("source_revision") ||
                    observed.at("preview_generation") != request.at("preview_generation"))
                    throw AuthoringError("stale_preview",
                                         "Read the current ready Timeline sequence source");
                const auto folder = destination(root, request);
                if (std::filesystem::exists(folder))
                    throw AuthoringError("output_exists", "Choose a new sequence frame folder");
                ui::SequenceFramePlan plan{request.at("start_seconds"),
                                           request.at("frames_per_second"),
                                           request.at("frame_count")};
                plan.validate(observed.at("duration"));
                ui::SequenceFrameOutput output;
                output.fixed = request.value("fixed_output", false);
                if (output.fixed) {
                    if (!request.contains("width") || !request.contains("height"))
                        throw AuthoringError("invalid_argument",
                                             "Fixed output needs width and height");
                    output.width = request.at("width");
                    output.height = request.at("height");
                } else if (request.contains("width") || request.contains("height"))
                    throw AuthoringError("invalid_argument",
                                         "Use fixed_output:true when specifying output dimensions");
                output.validate();
                if (!owner.begin_sequence(providers.sequence(), plan, folder,
                                          files.document.project(), scene.asset_id(), message,
                                          output))
                    throw AuthoringError("capture_busy", owner.status());
                owner.preview_requested_ = true;
                providers.show(ui::ViewImageTask::View::Game);
                return state(owner, files);
            }));
    }
};
} // namespace forge
