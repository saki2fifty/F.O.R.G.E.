#pragma once
#include "../navigation_behavior_authoring.hpp"
#include "../paint_texture_mesh.hpp"
#include "../paint_texture_source.hpp"
#include "../sprite_atlas_recipe.hpp"
#include "../tile_map_source.hpp"
#include "../tile_set_source.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "document.hpp"
#include "document_workspace.hpp"
#include <forge/material_graph.hpp>
#include <type_traits>

namespace forge {
// Private adapters borrow the same editor instances registered in DocumentWorkspace.
// They own neither source values, history, publication queues nor persistent IDs.
struct AssetDocumentAutomation {
    static NavigationBehaviorSource behavior_source(AssetId id, const Json& request) {
        const auto profile = request.value("behavior_profile", std::string("state_graph"));
        if (profile == "state_graph")
            return NavigationBehaviorSource::create(id);
        if (profile == "tree")
            return NavigationBehaviorSource::create_tree(id);
        throw AuthoringError("invalid_argument", "Behavior profile must be state_graph or tree");
    }
    static MaterialGraphSource graph_source(AssetId id, const Json& request) {
        const auto domain = request.value("domain", std::string("mesh"));
        if (domain != "mesh" && domain != "sprite")
            throw AuthoringError("invalid_argument", "Unknown material graph domain");
        return domain == "sprite" ? MaterialGraphSource::create_sprite(id)
                                  : MaterialGraphSource::create(id);
    }
    static std::vector<AssetId> atlas_images(const Json& request) {
        if (!request.contains("textures") || !request.at("textures").is_array() ||
            request.at("textures").empty() ||
            request.at("textures").size() > sprite_frame_region_limit)
            throw AuthoringError(
                "textures_required",
                "Sprite atlas creation/templates require 1 to 512 Texture UUIDs in textures");
        return request.at("textures").get<std::vector<AssetId>>();
    }
    static PaintTextureSource paint_source(AssetId id, const Json& request) {
        const auto canvas = request.value("canvas", Json::object());
        const auto interpretation = canvas.value("semantic", std::string("color"));
        if (interpretation != "color" && interpretation != "data")
            throw AuthoringError("invalid_argument", "Paint canvas semantic must be color or data");
        return {PaintTextureValue::create(
            id, canvas.value("width", 256u), canvas.value("height", 256u),
            interpretation == "color" ? TextureSemantic::Color : TextureSemantic::Data,
            canvas.value("background", PaintPixel{}))};
    }
    static PaintBrush paint_brush(const Json& specification) {
        PaintBrush brush;
        brush.color = specification.at("color").get<std::array<double, 4>>();
        brush.radius = specification.value("radius", 16.);
        brush.hardness = specification.value("hardness", .75);
        brush.opacity = specification.value("opacity", 1.);
        brush.erase = specification.value("erase", false);
        validate_paint_brush(brush);
        return brush;
    }
    template <class Editor> static bool transient(const Editor& editor) {
        bool active = false;
        if constexpr (requires { editor.has_unapplied_edits(); })
            active = active || editor.has_unapplied_edits();
        if constexpr (requires { editor.canvas.draft_dirty(); })
            active = active || editor.canvas.draft_dirty();
        if constexpr (requires { editor.graph_.draft_dirty(); })
            active = active || editor.graph_.draft_dirty();
        if constexpr (requires { editor.stroke_active(); })
            active = active || editor.stroke_active();
        if constexpr (requires {
                          editor.curve_dirty_;
                          editor.curve_drag_;
                          editor.drag_key_;
                      })
            active = active || editor.curve_dirty_ || editor.curve_drag_ ||
                     bool(editor.drag_key_) || editor.box_ || editor.pan_;
        if constexpr (requires {
                          editor.vertex_box_.active;
                          editor.uv_box_.active;
                      })
            active = active || editor.vertex_box_.active || editor.uv_box_.active ||
                     editor.preview_transform_ || editor.preview_extrude_ ||
                     editor.preview_inset_ || editor.preview_uv_ || editor.preview_uv_pack_ ||
                     editor.uv_drag_ || editor.stroke_active_;
        return active;
    }
    template <class Editor> static bool publishing(const Editor& editor) {
        if constexpr (requires { editor.publishing_; })
            return editor.publishing_;
        else
            return editor.pending();
    }
    template <class Editor> static bool publication_blocked(const Editor& editor) {
        return editor.dirty() || publishing(editor) || editor.save_ || editor.close_ ||
               transient(editor);
    }
    template <class Editor> static Json status(const Editor& editor, const SceneDocument& project) {
        const auto* doc = editor.document();
        if (!doc || doc->project() != project.project())
            return {{"open", false}};
        return {{"open", true},
                {"asset", doc->source().asset()},
                {"path", path_utf8(doc->locator())},
                {"session", doc->session()},
                {"revision", doc->revision()},
                {"dirty", editor.dirty()},
                {"source_dirty", doc->dirty()},
                {"publishing", publishing(editor)},
                {"save_requested", editor.save_},
                {"unpublished", editor.needs_publish_},
                {"transient_draft", transient(editor)},
                {"can_undo", editor.can_undo()},
                {"can_redo", editor.can_redo()},
                {"diagnostic", editor.error_}};
    }
    template <class Editor>
    static void check(Editor& editor, SceneDocument& project, const Json& request,
                      bool revision = true) {
        project.check_ownership();
        const auto* doc = editor.document();
        if (!doc || doc->project() != project.project())
            throw AuthoringError("not_open", "Open this asset document in the current project");
        if (request.at("document_session") != doc->session())
            throw AuthoringError("wrong_document", "This asset document session has changed");
        if (revision && request.at("document_revision") != doc->revision())
            throw AuthoringError("stale_document", "This asset document revision has changed");
    }
    static Json patch(Json value, const Json& operations) {
        for (const auto& op : operations) {
            const auto name = op.at("op").get<std::string>();
            if (name != "add" && name != "replace" && name != "remove" && name != "test")
                throw AuthoringError("invalid_patch",
                                     "Supported patch operations: add, replace, remove, test");
            value = value.patch(Json::array({op}));
            if (value.dump().size() > 8 * 1024 * 1024)
                throw AuthoringError("document_limit",
                                     "Edited JSON exceeds the source document limit");
        }
        return value;
    }
    template <class Editor> static void changed(Editor& editor) {
        if constexpr (requires {
                          editor.canvas.reset();
                          editor.function_.reset();
                      }) {
            editor.needs_publish_ = true;
            editor.function_.reset();
            editor.canvas.reset();
            editor.changed();
        } else if constexpr (requires { editor.changed(); })
            editor.changed();
        else {
            editor.needs_publish_ = true;
            if constexpr (requires { editor.reconcile_selection(); })
                editor.reconcile_selection();
        }
        editor.error_.clear();
    }
    template <class Editor>
    static ui::SourceDocumentAccess bind(Editor& editor, SceneDocument& project) {
        using Document = std::remove_cv_t<std::remove_pointer_t<decltype(editor.document())>>;
        using Source = std::remove_cvref_t<decltype(std::declval<Document>().source())>;
        constexpr bool tiles = std::is_same_v<Source, TileMapSource>;
        constexpr bool paint_texture = std::is_same_v<Source, PaintTextureSource>;
        constexpr bool atlas = std::is_same_v<Source, SpriteAtlasRecipe>;
        constexpr bool graph = std::is_same_v<Source, MaterialGraphSource>;
        constexpr bool behavior = std::is_same_v<Source, NavigationBehaviorSource>;
        return {
            Document::source_suffix,
            [&editor, &project](std::string_view action, const Json& request) -> Json {
                project.check_ownership();
                if (action == "status")
                    return status(editor, project);
                if constexpr (!paint_texture)
                    if (request.contains("canvas"))
                        throw AuthoringError(
                            "invalid_argument",
                            "canvas applies to paint texture creation/templates only");
                if (action == "template") {
                    if constexpr (!behavior)
                        if (request.contains("behavior_profile"))
                            throw AuthoringError(
                                "invalid_argument",
                                "behavior_profile applies to navigation_behavior templates only");
                    if constexpr (!graph)
                        if (request.contains("domain"))
                            throw AuthoringError("invalid_argument",
                                                 "domain applies to material graph templates only");
                    if constexpr (!atlas)
                        if (request.contains("textures"))
                            throw AuthoringError("invalid_argument",
                                                 "textures applies to Sprite atlas templates only");
                    if constexpr (!tiles)
                        if (request.contains("tile_set"))
                            throw AuthoringError("invalid_argument",
                                                 "tile_set applies to Tile Map templates only");
                    const auto id =
                        AssetId::generate(); // Preview only; creation allocates its own identity.
                    if constexpr (tiles) {
                        if (!request.contains("tile_set"))
                            throw AuthoringError("tile_set_required",
                                                 "Tile Map templates require a Tile Set UUID");
                        return TileMapValue::create(id, request.at("tile_set").get<AssetId>())
                            .metadata();
                    } else if constexpr (paint_texture)
                        return paint_source(id, request).document.metadata();
                    else if constexpr (atlas)
                        return Source::create(id, atlas_images(request)).document;
                    else if constexpr (graph)
                        return graph_source(id, request).document;
                    else if constexpr (behavior)
                        return behavior_source(id, request).document;
                    else
                        return Document::initial_source(id).document;
                }
                if (action == "open" || action == "create") {
                    if (editor.dirty() || editor.pending() || editor.save_ || editor.close_ ||
                        transient(editor))
                        throw AuthoringError("document_busy",
                                             "Save and close or explicitly discard this editor's "
                                             "current draft first");
                    if constexpr (requires { editor.sequence_mode(); })
                        if (editor.sequence_mode())
                            throw AuthoringError(
                                "document_busy",
                                "Close the active scene sequence before opening an asset timeline");
                    const auto path =
                        automation_relative_path(request.at("path").get<std::string>());
                    if (action == "open" &&
                        (request.contains("patch") || request.contains("tile_set") ||
                         request.contains("textures") || request.contains("domain") ||
                         request.contains("behavior_profile") || request.contains("canvas")))
                        throw AuthoringError(
                            "invalid_argument",
                            "Initial patch/tile_set/textures/domain/profile options apply "
                            "to creation only");
                    if constexpr (!behavior)
                        if (request.contains("behavior_profile"))
                            throw AuthoringError(
                                "invalid_argument",
                                "behavior_profile applies to navigation_behavior creation only");
                    if constexpr (!graph)
                        if (request.contains("domain"))
                            throw AuthoringError("invalid_argument",
                                                 "domain applies to material graph creation only");
                    if constexpr (!atlas)
                        if (request.contains("textures"))
                            throw AuthoringError("invalid_argument",
                                                 "textures applies to Sprite atlas creation only");
                    if constexpr (!tiles)
                        if (request.contains("tile_set"))
                            throw AuthoringError("invalid_argument",
                                                 "tile_set applies to Tile Map creation only");
                    if (action == "create") {
                        auto created =
                            Document::create(project.writer_guard(), path, [&](AssetId id) {
                                if constexpr (paint_texture) {
                                    auto source = paint_source(id, request);
                                    if (request.contains("patch"))
                                        source.document = source.document.with_metadata(
                                            patch(source.document.metadata(), request.at("patch")));
                                    return source;
                                } else if constexpr (tiles) {
                                    if (!request.contains("tile_set"))
                                        throw AuthoringError(
                                            "tile_set_required",
                                            "Create a Tile Set before its Tile Map");
                                    auto source = Source{TileMapValue::create(
                                        id, request.at("tile_set").get<AssetId>())};
                                    if (request.contains("patch"))
                                        source.document = source.document.with_metadata(
                                            patch(source.document.metadata(), request.at("patch")));
                                    return source;
                                } else {
                                    auto source = [&] {
                                        if constexpr (atlas)
                                            return Source::create(id, atlas_images(request));
                                        else if constexpr (graph)
                                            return graph_source(id, request);
                                        else if constexpr (behavior)
                                            return behavior_source(id, request);
                                        else
                                            return Document::initial_source(id);
                                    }();
                                    if (request.contains("patch"))
                                        source.document =
                                            patch(std::move(source.document), request.at("patch"));
                                    return source;
                                }
                            });
                        created.reset();
                    }
                    editor.load(project, path);
                    return status(editor, project);
                }
                check(editor, project, request,
                      action != "read" && action != "cells" && action != "pixels");
                auto& doc = *editor.document_;
                if (action == "read") {
                    Json value;
                    if constexpr (tiles || paint_texture)
                        value = doc.source().document.metadata();
                    else
                        value = doc.source().document;
                    const auto pointer = request.value("pointer", std::string{});
                    value = value.at(Json::json_pointer(pointer));
                    const auto offset = request.value("offset", std::size_t{});
                    const auto limit = request.value("limit", std::size_t(128));
                    Json result = status(editor, project);
                    if (value.is_array()) {
                        result["total"] = value.size();
                        Json page = Json::array();
                        for (std::size_t i = offset; i < value.size() && i - offset < limit; ++i)
                            page.push_back(std::move(value[i]));
                        result["value"] = std::move(page);
                    } else
                        result["value"] = std::move(value);
                    return result;
                }
                if (action == "pixels") {
                    if constexpr (!paint_texture)
                        throw AuthoringError("wrong_document_type",
                                             "Pixel queries require a paint texture");
                    else {
                        Json pixels = Json::array();
                        for (const auto& point : request.at("points"))
                            pixels.push_back(doc.source().document.pixel(
                                point.at(0).get<unsigned>(), point.at(1).get<unsigned>()));
                        return {{"pixels", std::move(pixels)}, {"revision", doc.revision()}};
                    }
                }
                if (action == "cells") {
                    if constexpr (!tiles)
                        throw AuthoringError("wrong_document_type",
                                             "Cell queries require a Tile Map");
                    else {
                        Json cells = Json::array();
                        for (const auto& query : request.at("cells")) {
                            const auto cell =
                                doc.source().document.cell(query.at("layer").get<std::uint64_t>(),
                                                           {query.at("x").get<std::int32_t>(),
                                                            query.at("y").get<std::int32_t>()});
                            auto value = query;
                            value["tile"] = cell.tile;
                            value["flags"] = cell.flags;
                            cells.push_back(std::move(value));
                        }
                        return {{"cells", std::move(cells)}, {"revision", doc.revision()}};
                    }
                }
                if (action == "cancel_pixels") {
                    if constexpr (paint_texture) {
                        editor.cancel_gesture();
                        return status(editor, project);
                    } else
                        throw AuthoringError("wrong_document_type",
                                             "Stroke cancellation requires a paint texture");
                }
                if (publishing(editor) || editor.save_ || editor.close_ || transient(editor))
                    throw AuthoringError(
                        "document_busy",
                        "Wait for publication or settle the active source gesture/close request");
                if (action == "patch") {
                    const auto previous = doc.revision();
                    doc.edit_source(previous,
                                    request.value("label", std::string("MCP source edit")),
                                    [&](Source& source) {
                                        if constexpr (tiles || paint_texture)
                                            source.document = source.document.with_metadata(patch(
                                                source.document.metadata(), request.at("patch")));
                                        else {
                                            const auto text =
                                                patch(source.document, request.at("patch")).dump();
                                            source = Source::parse(std::as_bytes(std::span(text)));
                                        }
                                        if (!doc.retains_history(source))
                                            throw AuthoringError(
                                                "history_limit",
                                                "Split this source edit to retain Undo and Redo");
                                    });
                    if (doc.revision() != previous)
                        changed(editor);
                } else if (action == "copy_pixels") {
                    if constexpr (!paint_texture)
                        throw AuthoringError("wrong_document_type",
                                             "Pixel copy requires a paint texture");
                    else
                        editor.prepare_copy(request.at("texture").get<AssetId>(), doc.revision());
                } else if (action == "stroke" || action == "mesh_stroke" || action == "fill") {
                    if constexpr (!paint_texture)
                        throw AuthoringError("wrong_document_type",
                                             "Pixel painting requires a paint texture");
                    else {
                        if (action == "fill") {
                            const auto previous = doc.revision();
                            doc.edit_source(previous, "Fill paint texture", [&](Source& source) {
                                source.document =
                                    source.document.fill(request.at("rgba").get<PaintPixel>());
                                if (!doc.retains_history(source))
                                    throw AuthoringError("history_limit",
                                                         "Fill exceeds the Undo and Redo budget");
                            });
                            if (doc.revision() != previous)
                                changed(editor);
                        } else {
                            const auto brush = paint_brush(request.at("brush"));
                            if (action == "mesh_stroke") {
                                PaintMeshSelection selection;
                                selection.mesh.id = request.at("mesh").get<AssetId>();
                                selection.material.id = request.at("material").get<AssetId>();
                                selection.mesh_revision =
                                    request.at("mesh_revision").get<std::string>();
                                selection.material_revision =
                                    request.at("material_revision").get<std::string>();
                                selection.slot =
                                    request.value("slot", std::string("baseColorTexture"));
                                selection.lod = request.value("lod", 0u);
                                std::vector<PaintMeshPoint> points;
                                for (const auto& sample : request.at("samples"))
                                    points.push_back({sample.value("part", 0u),
                                                      {sample.at("primitive").get<std::size_t>(),
                                                       sample.at("barycentric").get<Double3>()},
                                                      sample.value("break_before", false)});
                                editor.prepare_mesh_stroke(std::move(selection), std::move(points),
                                                           brush, doc.revision());
                            } else {
                                std::vector<PaintPoint> points;
                                for (const auto& point : request.at("points"))
                                    points.push_back(
                                        {point.at(0).get<double>(), point.at(1).get<double>()});
                                std::size_t previous_break = 0;
                                for (const auto& split : request.value("breaks", Json::array())) {
                                    const auto index = split.get<std::size_t>();
                                    if (index <= previous_break || index >= points.size())
                                        throw AuthoringError(
                                            "invalid_argument",
                                            "Stroke breaks must be increasing point indices from 1 "
                                            "to point count minus 1");
                                    points[index].break_before = true;
                                    previous_break = index;
                                }
                                editor.prepare_stroke(std::move(points), brush, doc.revision());
                            }
                        }
                    }
                } else if (action == "tile_collision" || action == "tile_animation" ||
                           action == "tile_autotile") {
                    if constexpr (std::is_same_v<Source, TileSetSource>) {
                        const bool animation = action == "tile_animation";
                        const bool autotile = action == "tile_autotile";
                        const auto previous = doc.revision();
                        doc.edit_source(
                            previous,
                            animation  ? "Change tile animation"
                            : autotile ? "Change tile neighbor rules"
                                       : "Change tile collision",
                            [&](Source& source) {
                                const auto tile = request.at("tile").get<std::uint64_t>();
                                if (animation)
                                    source.set_animation(tile, request.at("animation"));
                                else if (autotile)
                                    source.set_autotile(tile, request.at("autotile"));
                                else
                                    source.set_collision(tile, request.at("collision"));
                                if (!doc.retains_history(source))
                                    throw AuthoringError(
                                        "history_limit",
                                        "Split this source edit to retain Undo and Redo");
                            });
                        if (doc.revision() != previous)
                            changed(editor);
                    } else
                        throw AuthoringError("wrong_document_type",
                                             "Tile profiles require a Tile Set");
                } else if (action == "paint") {
                    if constexpr (!tiles)
                        throw AuthoringError("wrong_document_type",
                                             "Cell painting requires a Tile Map");
                    else {
                        std::vector<TileMapCellEdit> edits;
                        for (const auto& cell : request.at("cells"))
                            edits.push_back({cell.at("layer").get<std::uint64_t>(),
                                             {cell.at("x").get<std::int32_t>(),
                                              cell.at("y").get<std::int32_t>()},
                                             {cell.at("tile").get<std::uint64_t>(),
                                              cell.value("flags", std::uint32_t{})}});
                        const auto previous = doc.revision();
                        doc.edit_source(previous,
                                        request.value("label", std::string("MCP paint cells")),
                                        [&](Source& source) {
                                            source.document = source.document.paint(edits);
                                            if (!doc.retains_history(source))
                                                throw AuthoringError(
                                                    "history_limit",
                                                    "Paint fewer cells to retain Undo and Redo");
                                        });
                        if (doc.revision() != previous)
                            changed(editor);
                    }
                } else if (action == "undo" || action == "redo") {
                    const auto previous = doc.revision();
                    if (action == "undo")
                        editor.undo();
                    else
                        editor.redo();
                    if (doc.revision() == previous)
                        throw AuthoringError("history_empty",
                                             "No asset document history in this direction");
                } else if (action == "save") {
                    doc.save(); // Synchronous baseline/identity preflight, then the usual
                                // publication path.
                    editor.request_save();
                } else if (action == "close") {
                    if (editor.dirty() && !request.value("discard", false))
                        throw AuthoringError(
                            "unsaved_document",
                            "Save and publish this source or explicitly discard its draft");
                    editor.finish_close();
                } else
                    throw AuthoringError("unknown_operation", "Unknown source document operation");
                return status(editor, project);
            },
            [&editor, &project](AssetId id) {
                const auto* doc = editor.document();
                return doc && doc->project() == project.project() && doc->source().asset() == id &&
                       publication_blocked(editor);
            },
            [&editor, &project](AssetId id) {
                const auto* doc = editor.document();
                if (!doc || doc->project() != project.project() || doc->source().asset() != id)
                    return;
                // Graph Save publishes through the shader owner. Its acknowledgement
                // retains the editor's history and current preview.
                if constexpr (requires { editor.publishing_; }) {
                    if (editor.publishing_) {
                        editor.source_published(id);
                        return;
                    }
                }
                if (publication_blocked(editor))
                    return;
                try {
                    if constexpr (requires { editor.source_published(project, id); })
                        editor.source_published(project, id);
                    else {
                        const auto path = doc->locator();
                        editor.load(project, path);
                    }
                } catch (const std::exception& e) {
                    // One failed reload must not prevent other catalog consumers
                    // from observing a publication that has already committed.
                    editor.error_ = e.what();
                }
            },
            [&editor, &project](AssetId id,
                                const std::optional<std::filesystem::path>& destination) {
                const auto* doc = editor.document();
                if (!doc || doc->project() != project.project() || doc->source().asset() != id)
                    return;
                if (destination)
                    editor.open(project, *destination);
                else
                    editor.request_close();
            }};
    }
};
} // namespace forge
