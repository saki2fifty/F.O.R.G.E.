#pragma once
#include "../asset_bytes.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "flecs_script.hpp"
namespace forge {
struct ScriptAutomation {
    static Json state(ui::FlecsScriptEditor& owner, EditorFiles& files, bool text = false) {
        files.document.check_ownership();
        if (!owner.is_open() || owner.root_ != files.document.project())
            return {{"open", false}, {"busy", false}};
        const std::string contents(owner.text_.data());
        const auto encoded = Json{{"session", owner.session_}, {"text", contents}}.dump();
        const auto digest =
            asset_detail::content_digest(std::as_bytes(std::span(encoded.data(), encoded.size())));
        Json value{{"open", true},
                   {"asset", owner.record_.id},
                   {"path", path_utf8(owner.record_.source)},
                   {"session", owner.session_},
                   {"state", digest},
                   {"dirty", owner.dirty()},
                   {"busy", owner.pending()},
                   {"operation", owner.operation_},
                   {"error", owner.error_},
                   {"line", owner.error_line_},
                   {"column", owner.error_column_},
                   {"preview_matches_draft", owner.previous_ == contents && !owner.result_.empty()},
                   {"preview_bytes", owner.result_.size()}};
        if (text)
            value["text"] = contents;
        return value;
    }
    static std::vector<LiveAuthoringMethod> methods(ui::FlecsScriptEditor& owner,
                                                    EditorFiles& files,
                                                    const ui::AutomationPermissions& permissions,
                                                    std::function<std::string()> unavailable) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(
            automation_method("script.read",
                              "Read the actual Flecs Script data/world-recipe draft and isolated "
                              "preview status. This is the existing diagnostic Preview world, not "
                              "project C++ gameplay or an authored-scene writer.",
                              "assets.read", automation_object(), false,
                              [&owner, &files](const Json&) { return state(owner, files, true); }));
        result.push_back(automation_method(
            "script.result",
            "Read a bounded page of the last successful native script preview; "
            "failed candidates retain it. Offsets count UTF-8 bytes.",
            "assets.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 262144)}}),
            false, [&owner, &files](const Json& request) {
                auto status = state(owner, files);
                if (!status.at("open").get<bool>())
                    return status;
                const auto& text = owner.result_;
                auto start = std::min(request.value("offset", std::size_t{}), text.size());
                const auto continuation = [&](std::size_t i) {
                    return (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80;
                };
                while (start < text.size() && continuation(start))
                    ++start;
                auto end =
                    std::min(text.size(), start + request.value("limit", std::size_t(65536)));
                while (end > start && end < text.size() && continuation(end))
                    --end;
                status["text"] = text.substr(start, end - start);
                status["next_offset"] = end;
                return status;
            }));
        if (!permissions.assets)
            return result;
        const auto ready = [&owner, &files, unavailable] {
            files.document.check_ownership();
            const auto reason = unavailable ? unavailable() : "";
            if (!reason.empty())
                throw AuthoringError("script_busy", reason);
            if (owner.close_requested_ || owner.save_requested_)
                throw AuthoringError("script_busy", "Settle the script save/close request");
        };
        result.push_back(automation_method(
            "script.register",
            "Create the existing example if missing or register an existing "
            "contained .flecs source, then open its actual editor. Existing "
            "source is never overwritten; registration is separate from Undo.",
            "assets.write",
            automation_object(
                {{"path", automation_text(2047, 1)}, {"create", {{"type", "boolean"}}}}, {"path"}),
            true, [&owner, &files, ready](const Json& request) {
                ready();
                const auto relative =
                    automation_relative_path(request.at("path").get<std::string>());
                for (const auto& part : relative)
                    if (part == ".forge" || part == "Native" || part == "Builds")
                        throw AuthoringError("invalid_path", "Choose an authored data-script path");
                owner.register_source(files.document, relative, request.value("create", true));
                return state(owner, files, true);
            }));
        result.push_back(automation_method(
            "script.open", "Open a registered Flecs Script in its actual source/preview owner.",
            "assets.write", automation_object({{"asset", automation_uuid()}}, {"asset"}), true,
            [&owner, &files, ready](const Json& request) {
                ready();
                if (owner.pending())
                    throw AuthoringError("script_busy", "Wait for the script preview");
                const auto catalog = AssetCatalog::open_project(files.document.project());
                const auto id = request.at("asset").get<AssetId>();
                const auto found = catalog.records().find(id);
                if (found == catalog.records().end() ||
                    found->second.type != FlecsScriptAsset::type)
                    throw AuthoringError("invalid_asset", "Choose a registered Flecs Script");
                owner.open(files.document, found->second);
                return state(owner, files, true);
            }));
        for (const auto* action : {"replace", "save", "preview", "close"}) {
            Json values{{"state", automation_digest()}}, keys{"state"};
            if (std::string_view(action) == "replace") {
                values["text"] = automation_text(1024 * 1024);
                keys.push_back("text");
            } else if (std::string_view(action) == "preview")
                values["fresh"] = {{"type", "boolean"}};
            else if (std::string_view(action) == "close")
                values["discard"] = {{"type", "boolean"}};
            result.push_back(automation_method(
                std::string("script.") + action,
                std::string(action) +
                    " the exact existing Flecs Script draft. Save checks the disk "
                    "baseline. Preview evaluates only a bounded isolated diagnostic world and "
                    "preserves the last good result on failure. Replacement has no Scene or "
                    "document-history step; the native source widget owns its local input state.",
                "assets.write", automation_object(values, keys), true,
                [&owner, &files, ready, action](const Json& request) {
                    ready();
                    const auto observed = state(owner, files);
                    if (!observed.at("open").get<bool>() ||
                        observed.at("state") != request.at("state"))
                        throw AuthoringError("stale_script",
                                             "Read the current script draft/session first");
                    if (owner.pending())
                        throw AuthoringError("script_busy", "Wait for the script preview");
                    const auto operation = std::string_view(action);
                    if (operation == "replace") {
                        const auto text = request.at("text").get<std::string>();
                        if (text.size() >= owner.text_.size() ||
                            text.find('\0') != std::string::npos)
                            throw AuthoringError(
                                "invalid_source",
                                "Use UTF-8 text without NUL within the script budget");
                        (void)Json(text).dump();
                        std::copy(text.begin(), text.end(), owner.text_.begin());
                        owner.text_[text.size()] = 0;
                        owner.search_position_ = 0;
                        owner.select_start_ = -1;
                    } else if (operation == "save") {
                        if (!owner.save(files.document))
                            throw AuthoringError("source_conflict", owner.error_);
                    } else if (operation == "preview")
                        owner.apply(request.value("fresh", false));
                    else {
                        if (owner.dirty() && !request.value("discard", false))
                            throw AuthoringError("unsaved_source",
                                                 "Save or explicitly discard this script draft");
                        owner.open_ = false;
                        owner.cancel_.request_stop();
                    }
                    return state(owner, files);
                }));
        }
        result.push_back(automation_method(
            "script.cancel",
            "Cancel the exact current isolated script preview without changing "
            "its source or previous successful result.",
            "assets.write", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            true,
            [&owner, &files](const Json& request) {
                files.document.check_ownership();
                if (owner.root_ != files.document.project() || !owner.pending() ||
                    request.at("operation") != owner.operation_)
                    throw AuthoringError("wrong_job", "Choose this project's exact script preview");
                owner.cancel_.request_stop();
                return state(owner, files);
            },
            true));
        return result;
    }
};
} // namespace forge
