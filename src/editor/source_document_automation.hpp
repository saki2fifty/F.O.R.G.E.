#pragma once
#include "../tile_map_source.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "document_workspace.hpp"
#include <type_traits>

namespace forge {
// Private adapters borrow the same editor instances registered in DocumentWorkspace.
// They own neither source values, history, publication queues nor persistent IDs.
struct AssetDocumentAutomation {
    template <class Editor> static bool transient(const Editor& editor) {
        bool active = false;
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
        return {
            Document::source_suffix,
            [&editor, &project](std::string_view action, const Json& request) -> Json {
                project.check_ownership();
                if (action == "status")
                    return status(editor, project);
                if (action == "template") {
                    const auto id =
                        AssetId::generate(); // Preview only; creation allocates its own identity.
                    if constexpr (tiles) {
                        if (!request.contains("tile_set"))
                            throw AuthoringError("tile_set_required",
                                                 "Tile Map templates require a Tile Set UUID");
                        return TileMapValue::create(id, request.at("tile_set").get<AssetId>())
                            .metadata();
                    } else
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
                        (request.contains("patch") || request.contains("tile_set")))
                        throw AuthoringError(
                            "invalid_argument",
                            "Initial patch/tile_set options apply to creation only");
                    if constexpr (!tiles)
                        if (request.contains("tile_set"))
                            throw AuthoringError("invalid_argument",
                                                 "tile_set applies to Tile Map creation only");
                    if (action == "create") {
                        auto created =
                            Document::create(project.writer_guard(), path, [&](AssetId id) {
                                if constexpr (tiles) {
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
                                    auto source = Document::initial_source(id);
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
                check(editor, project, request, action != "read" && action != "cells");
                auto& doc = *editor.document_;
                if (action == "read") {
                    Json value;
                    if constexpr (tiles)
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
                if (publishing(editor) || editor.save_ || editor.close_ || transient(editor))
                    throw AuthoringError(
                        "document_busy",
                        "Wait for publication or settle the active source gesture/close request");
                if (action == "patch") {
                    const auto previous = doc.revision();
                    doc.edit_source(previous,
                                    request.value("label", std::string("MCP source edit")),
                                    [&](Source& source) {
                                        if constexpr (tiles)
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
            }};
    }
};
} // namespace forge
