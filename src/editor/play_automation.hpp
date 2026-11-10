#pragma once
#include "../asset_bytes.hpp"
#include "../cpu_profile_trace.hpp"
#include "../input_recording_files.hpp"
#include "../ui_control_access.hpp"
#include "../ui_field_keys.hpp"
#include "../ui_range_numeric.hpp"
#include "animation_playback.hpp"
#include "audio_source.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "event_time.hpp"
#include "files.hpp"
#include "play.hpp"
#include "runtime_tuning_review.hpp"
#include <forge/ui_protocol.hpp>

namespace forge {
struct PlayAutomationProviders {
    // Shared editor policies include asset readiness, source freshness, physical
    // input release and presentation cleanup. No second launch/input owner.
    std::function<void()> start;
    std::function<void()> start_paused;
    std::function<void(const std::string&)> control;
    std::function<std::string()> control_unavailable;
    std::function<void(const std::string&)> select_entity;
    std::function<bool()> prepare_input;
    std::function<GamepadTopology()> gamepads;
    std::function<GamepadTopology(const GamepadAssignment&)> assign_gamepad;
    std::function<GamepadMotion()> gamepad_motion;
    std::function<GamepadMotion(const GamepadSensorConfig&)> configure_gamepad_sensor;
    std::function<bool(const std::string&, const std::string&, const std::optional<std::string>&)>
        ui_command;
    std::function<void()> reload_ui;
    std::function<Json()> ui_localization;
    std::function<Json()> ui_application_states;
    std::function<Json(const std::string&)> ui_fields;
    std::function<Json(const std::string&, const std::string&, std::uint64_t, const std::string&,
                       const std::string&)>
        ui_field_action;
    std::function<Json(const std::string&)> ui_controls;
    std::function<Json(const std::string&, const std::string&, std::size_t, std::size_t)>
        ui_options;
    std::function<Json(const std::string&, const std::string&, std::uint64_t, const std::string&,
                       bool, std::size_t, const std::string&)>
        ui_control_action;
    std::function<Json(const std::string&)> ui_ranges;
    std::function<Json(const std::string&, const std::string&, std::uint64_t, double)>
        ui_range_value;
    std::function<Json(const std::string&)> ui_buttons;
    std::function<Json(const std::string&, const std::string&, std::uint64_t)> ui_press;
};
struct PlayAutomation {
    static Json state(const PlaySession& play) {
        return {{"active", play.active()},
                {"ready", play.ready()},
                {"control_ready", play.control_ready()},
                {"launch", play.launch_id()},
                {"session", play.session()},
                {"generation", play.world_generation()},
                {"snapshot_version", play.snapshot_version()},
                {"paused", play.paused()},
                {"timing", play.timing()},
                {"status", play.status()},
                {"input_recording", play.input_recording_status()},
                {"input_recording_pending", play.input_recording_pending()},
                {"input_recording_ready", !play.input_recording_document().is_null()},
                {"input_recording_error", play.input_recording_error()},
                {"sdk", play.sdk_play()},
                {"loading", play.sdk_loading()},
                {"tune_pending", play.property_tune_pending()},
                {"tune_ack", play.property_tune_ack()},
                {"application_state_pending", play.application_state_pending()},
                {"application_state_error", play.application_state_error()}};
    }
    static void current(const PlaySession& play, EditorFiles& files,
                        const PlayAutomationProviders& providers, const Json& request) {
        files.document.check_ownership();
        if (!play.active() || request.at("launch") != play.launch_id() ||
            request.at("session") != play.session() ||
            request.at("generation") != play.world_generation())
            throw AuthoringError("stale_play", "Read the current Play launch/session/world first");
        if (providers.control_unavailable) {
            const auto reason = providers.control_unavailable();
            if (!reason.empty())
                throw AuthoringError("play_busy", reason);
        }
    }
    static Json properties() {
        return {{"launch", automation_uuid()},
                {"session", automation_text(128)},
                {"generation", automation_integer()}};
    }
    static Json required() { return {"launch", "session", "generation"}; }
    static Json review(const RuntimeTuningReview& owner) {
        Json fields = Json::array();
        for (const auto& field : owner.fields())
            fields.push_back({{"component", field.component},
                              {"property", field.property},
                              {"label", field.label},
                              {"before", field.before},
                              {"after", field.after},
                              {"selected", field.selected}});
        const auto encoded = Json{{"session", owner.session()}, {"fields", fields}}.dump();
        const auto digest =
            asset_detail::content_digest(std::as_bytes(std::span(encoded.data(), encoded.size())));
        return {{"pending", owner.pending()},
                {"session", owner.session()},
                {"state", digest},
                {"fields", std::move(fields)},
                {"status", owner.status()}};
    }
    static std::vector<LiveAuthoringMethod> methods(Scene& scene, EditorFiles& files,
                                                    PlaySession& play, RuntimeTuningReview& tuning,
                                                    PlayAutomationProviders providers,
                                                    const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> methods;
        methods.push_back(automation_method(
            "play.status",
            "Read the owning isolated runtime's launch, session, world, clock and "
            "live-tuning acknowledgement. Clock timing includes unscaled_time and "
            "unscaled_delta in seconds plus unscaled_sequence; these observations continue "
            "during Pause and zero speed. Reads do not advance time or authored data.",
            "play.read", automation_object(), false, [&files, &play](const Json&) {
                files.document.check_ownership();
                return state(play);
            }));
        methods.push_back(automation_method(
            "play.snapshot",
            "Page actual effective runtime entities, including current navigation failure "
            "details and sampled coordinates where available, at the reported snapshot "
            "version. Running snapshots can change between reads; pause first "
            "for a stable inspection. Opted-in wind includes its borrowed simulation_seconds "
            "geometry sample. This never reads an authored-world copy.",
            "play.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [&files, &play](const Json& request) {
                files.document.check_ownership();
                if (!play.ready())
                    throw AuthoringError("not_ready", "Wait for a live Play world");
                const auto& snapshot = play.effective_snapshot();
                auto result = state(play);
                result["asset_id"] = snapshot.at("asset_id");
                if (snapshot.contains("simulation_seconds"))
                    result["simulation_seconds"] = snapshot.at("simulation_seconds");
                result["entities"] = Json::array();
                const auto& entities = snapshot.at("entities");
                const auto offset = request.value("offset", std::size_t{}),
                           limit = request.value("limit", std::size_t(128));
                result["total"] = entities.size();
                for (auto i = offset; i < entities.size() && i - offset < limit; ++i)
                    result["entities"].push_back(entities[i]);
                return result;
            }));
        methods.push_back(automation_method(
            "play.ui.fields",
            "Read bounded actual native text/password/textarea fields in a current UI document "
            "instance from play.ui. Reports the presentation revision, unique element IDs, "
            "visibility, enabled/focus/selection state and admitted text values. Password "
            "plaintext is omitted. At most4096 DOM nodes/64fields and1024UTF-8 bytes per value; "
            "truncated observations cannot be used for input. Null value means no live presenter. "
            "This observes runtime UI, never authored RML/Scene/history.",
            "play.read", automation_object({{"instance", automation_text(256, 1)}}, {"instance"}),
            false, [&files, &play, fields = providers.ui_fields](const Json& request) {
                files.document.check_ownership();
                auto result = state(play);
                result["value"] = play.ready() && fields ? fields(request.at("instance")) : Json{};
                return result;
            }));
        methods.push_back(automation_method(
            "play.ui.controls",
            "Read bounded actual native checkbox/radio/dropdown/progress controls in one current "
            "UI document instance. Reports session/world/presentation, unique IDs, native state, "
            "visibility/enabled/focus and readonly progress. Text/password controls stay in "
            "play.ui.fields; no password values enter this view. At most 4096 native nodes and "
            "64 controls. No focus, callback, source or Scene mutation; null without a ready "
            "presenter.",
            "play.read", automation_object({{"instance", automation_text(256, 1)}}, {"instance"}),
            false, [&files, &play, controls = providers.ui_controls](const Json& request) {
                files.document.check_ownership();
                auto result = state(play);
                result["value"] =
                    play.ready() && controls ? controls(request.at("instance")) : Json{};
                return result;
            }));
        methods.push_back(automation_method(
            "play.ui.options",
            "Read a bounded page of current native dropdown options by instance/unique element. "
            "Optional offset defaults to0, limit to16 (1..32). Returns index, native raw "
            "text/value "
            "availability, enabled/visibility/selection, next_offset and observed option state "
            "digest. Text/value cap1024UTF-8bytes; text traversal256nodes. Pending native children "
            "report ready:false and are never flushed by this read. Use option_state/index with "
            "play.ui.select; no DOM serialization, nested control values, focus or change events.",
            "play.read",
            automation_object({{"instance", automation_text(256, 1)},
                               {"element", automation_text(256, 1)},
                               {"offset", automation_integer(0, ui_control_nodes)},
                               {"limit", automation_integer(1, ui_form_options_page_limit)}},
                              {"instance", "element"}),
            false, [&files, &play, options = providers.ui_options](const Json& request) {
                files.document.check_ownership();
                auto result = state(play);
                result["value"] = play.ready() && options
                                      ? options(request.at("instance"), request.at("element"),
                                                request.value("offset", std::size_t{}),
                                                request.value("limit", std::size_t(16)))
                                      : Json{};
                return result;
            }));
        methods.push_back(automation_method(
            "play.ui.buttons",
            "Read current native literal buttons and input type=button in one UI instance. "
            "Independent 64-button/4096-node scan preserves text/form counters. Reports scope, "
            "unique IDs, visibility, focus and pressable facts. Generic buttons have enabled:null; "
            "input buttons expose native disabled state. Generic captions/native input values "
            "are bounded to 1024 UTF-8 bytes; nested form/password values are not read. No focus, "
            "callback, source or Scene mutation; null without a ready presenter.",
            "play.read", automation_object({{"instance", automation_text(256, 1)}}, {"instance"}),
            false, [&files, &play, buttons = providers.ui_buttons](const Json& request) {
                files.document.check_ownership();
                auto result = state(play);
                result["value"] =
                    play.ready() && buttons ? buttons(request.at("instance")) : Json{};
                return result;
            }));
        methods.push_back(automation_method(
            "play.ui.ranges",
            "Read native input type=range sliders in one current UI instance. Independent "
            "64-slider/4096-node budget; unique IDs, focus/visibility/disabled and numeric "
            "admission facts. Value is the native six-decimal string, not the authored attribute "
            "or private float precision. Admitted min/max/step and native orientation are exposed. "
            "No focus, events or source/Scene writes; null without a ready presenter.",
            "play.read", automation_object({{"instance", automation_text(256, 1)}}, {"instance"}),
            false, [&files, &play, ranges = providers.ui_ranges](const Json& request) {
                files.document.check_ownership();
                auto result = state(play);
                result["value"] = play.ready() && ranges ? ranges(request.at("instance")) : Json{};
                return result;
            }));
        for (const auto* kind : {"schema", "diagnostics", "physics", "input", "ui", "audio",
                                 "animation", "profile", "application_states"})
            methods.push_back(automation_method(
                std::string("play.") + kind,
                std::string("Read cached ") + kind +
                    " observations published by the actual Play owner, correlated with its "
                    "launch/session/world. Watch-dependent values require an explicit watch.",
                "play.read", automation_object(), false,
                [&files, &play, kind, localization = providers.ui_localization,
                 states = providers.ui_application_states](const Json&) {
                    files.document.check_ownership();
                    auto result = state(play);
                    const auto key = std::string_view(kind);
                    if (key == "schema")
                        result["value"] = play.runtime_schema();
                    else if (key == "diagnostics")
                        result["value"] = play.diagnostics();
                    else if (key == "physics")
                        result["value"] = play.physics_status();
                    else if (key == "input")
                        result["value"] = play.input_status();
                    else if (key == "application_states")
                        result["value"] = play.application_states();
                    else if (key == "ui") {
                        result["value"] = play.ui_snapshot();
                        result["ack"] = play.ui_ack();
                        result["presentation_localization"] =
                            localization ? localization() : Json{};
                        result["presentation_application_states"] = states ? states() : Json{};
                    } else if (key == "audio") {
                        result["value"] = play.audio_status();
                        result["source"] = play.audio_source_status();
                        result["error"] = play.audio_source_error();
                        result["mix_error"] = play.audio_mix_error();
                    } else if (key == "animation") {
                        result["value"] = play.animation_status();
                        result["error"] = play.animation_error();
                    } else {
                        result["value"] = play.cpu_profile();
                        result["error"] = play.cpu_profile_error();
                    }
                    return result;
                }));
        methods.push_back(automation_method(
            "play.profile.trace",
            "Read up to128 measured CPU scopes per page from the cached Play profile as "
            "Trace Event JSON. Refresh after collection. Reuse the returned profile_state "
            "for subsequent pages; changed snapshots reject it. Duration-only SDK/GPU/native "
            "lifetime records are not given invented timestamps. Admitted native worker "
            "records retain stage identity; overlap lanes stay within their stage. Native "
            "stage/track IDs are not OS thread IDs. Editor-wide traces remain in its UI.",
            "play.read",
            automation_object({{"offset", automation_integer(0, 512)},
                               {"limit", automation_integer(1, 128)},
                               {"profile_state", automation_text(64, 64)}}),
            false, [&files, &play](const Json& request) {
                files.document.check_ownership();
                const auto& profile = play.cpu_profile();
                if (!profile.contains("samples"))
                    throw AuthoringError("not_ready",
                                         "Collect and refresh the current Play profile first");
                const auto live = state(play);
                if (profile.value("world_generation", std::uint64_t{}) !=
                    live.at("generation").get<std::uint64_t>())
                    throw AuthoringError("not_ready", "Refresh the current world's profile first");
                const auto signature =
                    Json{{"launch", play.launch_id()},
                         {"session", play.session()},
                         {"generation", profile.at("world_generation")},
                         {"sequence", profile.at("sequence")},
                         {"epoch", profile.at("epoch")},
                         {"native_systems_dropped",
                          profile.value("native_systems_dropped", std::uint64_t{})}}
                        .dump();
                const auto cursor = asset_detail::content_digest(
                    std::as_bytes(std::span(signature.data(), signature.size())));
                if (request.contains("profile_state") && request.at("profile_state") != cursor)
                    throw AuthoringError("stale_profile",
                                         "Refresh the profile before reading another page");
                const auto offset = request.value("offset", std::size_t{});
                const auto limit = request.value("limit", std::size_t(128));
                if (offset > profile_record_capacity || !limit || limit > 128)
                    throw AuthoringError("invalid_range",
                                         "Trace pages require offset0..512 and limit1..128");
                return Json{{"launch", play.launch_id()},
                            {"session", play.session()},
                            {"generation", profile.at("world_generation")},
                            {"profile_state", cursor},
                            {"trace", cpu_scope_trace(profile, "FORGE Play runtime",
                                                      ui::event_time_local(), offset, limit)}};
            }));
        methods.push_back(automation_method(
            "play.log", "Read the existing timestamped runtime log, bounded to its native log cap.",
            "play.read", automation_object(), false, [&files, &play](const Json&) {
                files.document.check_ownership();
                return Json{{"launch", play.launch_id()},
                            {"session", play.session()},
                            {"text", automation_text_value(play.timed_log())}};
            }));
        methods.push_back(automation_method(
            "play.tuning.read",
            "Read the existing frozen review for keeping selected live values "
            "after Stop; changes are never automatically authored.",
            "play.read", automation_object(), false, [&files, &tuning](const Json&) {
                files.document.check_ownership();
                return review(tuning);
            }));
        methods.push_back(automation_method(
            "play.controls",
            "List actual semantic controls and value kinds. Single-contact tap, double_tap, "
            "long_press and swipe_left/right/up/down are digital recognition pulses; submit "
            "a 1/0 pair to test them. Touch gesture pan_x/y are "
            "normalized image deltas, pinch is log-scale delta, rotation is clockwise "
            "radians. Relative values accumulate for one latch in their input domain.",
            "play.read", automation_object(), false, [&files](const Json&) {
                files.document.check_ownership();
                Json controls = Json::array();
                for (const auto& control : input_controls())
                    controls.push_back({{"id", control.id},
                                        {"relative", control.relative},
                                        {"digital", control.digital},
                                        {"minimum", control.digital || control.unipolar ? 0
                                                    : control.relative                  ? -1000000
                                                                                        : -1},
                                        {"maximum", control.relative ? 1000000 : 1}});
                return Json{{"controls", std::move(controls)}};
            }));
        methods.push_back(automation_method(
            "play.gamepads",
            "Read the actual host's transient controller connections, "
            "logical slots 1.." +
                std::to_string(gamepad_slot_limit) +
                " and topology revision. Opaque connection tokens are not saved "
                "identities. An unavailable host is distinct from an empty device list.",
            "play.read", automation_object(), false, [&files, providers](const Json&) {
                files.document.check_ownership();
                return Json(providers.gamepads ? providers.gamepads() : GamepadTopology{});
            }));
        methods.push_back(automation_method(
            "play.gamepads.motion",
            "Read raw controller acceleration (m/s² including gravity) and angular speed "
            "(rad/s) in the natural device frame: X right, Y up, Z toward the user. "
            "Supported, enabled and measured are distinct. Samples require routed gameplay "
            "input; an absent measurement is not measured zero. Read does not advance time.",
            "play.read", automation_object(), false, [&files, providers](const Json&) {
                files.document.check_ownership();
                return Json(providers.gamepad_motion ? providers.gamepad_motion()
                                                     : GamepadMotion{});
            }));
        if (!permissions.execute)
            return methods;
        methods.push_back(automation_method(
            "play.gamepads.assign",
            "Assign a current connection to slot 1.." + std::to_string(gamepad_slot_limit) +
                ", or 0 to "
                "unassign. Use the revision and device token from play.gamepads. An occupied "
                "slot is cleared first; only affected controls and feedback are released. "
                "This transient host edit has no authored Undo or saved identity.",
            "play.execute",
            automation_object({{"revision", automation_integer(1)},
                               {"device", automation_text(128, 1)},
                               {"slot", automation_integer(0, gamepad_slot_limit)}},
                              {"revision", "device", "slot"}),
            true,
            [&files, providers](const Json& request) {
                files.document.check_ownership();
                if (providers.control_unavailable) {
                    const auto reason = providers.control_unavailable();
                    if (!reason.empty())
                        throw AuthoringError("play_busy", reason);
                }
                if (!providers.assign_gamepad)
                    throw AuthoringError("unavailable",
                                         "This host has no gamepad assignment adapter");
                const auto value = Json{{"revision", request.at("revision")},
                                        {"device", request.at("device")},
                                        {"slot", request.at("slot")}}
                                       .get<GamepadAssignment>();
                return Json(providers.assign_gamepad(value));
            },
            true));
        methods.push_back(automation_method(
            "play.gamepads.sensor",
            "Enable or disable a current connection's accelerometer or gyroscope using "
            "the current revision/device from play.gamepads or play.gamepads.motion. "
            "Sensors default off. Configuration is transient for this host session, "
            "advances the shared device revision and has no authored Undo or saved identity.",
            "play.execute",
            automation_object(
                {{"revision", automation_integer(1)},
                 {"device", automation_text(128, 1)},
                 {"sensor", {{"type", "string"}, {"enum", {"accelerometer", "gyroscope"}}}},
                 {"enabled", {{"type", "boolean"}}}},
                {"revision", "device", "sensor", "enabled"}),
            true,
            [&files, providers](const Json& request) {
                files.document.check_ownership();
                if (providers.control_unavailable) {
                    const auto reason = providers.control_unavailable();
                    if (!reason.empty())
                        throw AuthoringError("play_busy", reason);
                }
                if (!providers.configure_gamepad_sensor)
                    throw AuthoringError("not_ready", "No native gamepad motion host is available");
                const auto value = Json{{"revision", request.at("revision")},
                                        {"device", request.at("device")},
                                        {"sensor", request.at("sensor")},
                                        {"enabled", request.at("enabled")}}
                                       .get<GamepadSensorConfig>();
                return Json(providers.configure_gamepad_sensor(value));
            },
            true));
        methods.push_back(automation_method(
            "play.start",
            "Start through the editor's existing source/asset/settings readiness "
            "policy. Native project code runs with this account's OS access. "
            "Save/build stale C++ first; this never silently builds or discards drafts.",
            "play.execute", automation_object({{"paused", {{"type", "boolean"}}}}), true,
            [&files, &play, providers](const Json& request) {
                files.document.check_ownership();
                if (play.active())
                    throw AuthoringError("play_busy", "Stop the current runtime first");
                if (request.value("paused", false)) {
                    if (!providers.start_paused)
                        throw AuthoringError("not_supported", "Paused launch is unavailable");
                    providers.start_paused();
                } else
                    providers.start();
                if (!play.active())
                    throw AuthoringError("not_ready", "The editor did not start a runtime");
                return state(play);
            }));
        auto recording_properties = properties();
        recording_properties.update(
            {{"operation", {{"type", "string"}, {"enum", {"begin", "finish", "read", "replay"}}}},
             {"path", automation_text(2047, 1)}});
        auto recording_required = required();
        recording_required.push_back("operation");
        methods.push_back(automation_method(
            "play.input_recording.control",
            "Record/replay fixed gameplay ActionId snapshots through the current runtime. "
            "Begin/replay require a fresh paused tick-zero launch; replay loads a project-relative "
            ".json path and requires matching Scene/settings/build, input map and Hz. Finish stops "
            "capture or cancels replay; Read retrieves a finished recording for Save. Observe "
            "play.status for acknowledgement/errors. This has no Scene Undo and does not record "
            "UI/text, Scene transitions or arbitrary native state.",
            "play.execute", automation_object(recording_properties, recording_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                const auto operation = request.at("operation").get<std::string>();
                if ((operation == "replay") != request.contains("path"))
                    throw AuthoringError("invalid_arguments",
                                         "Only replay requires a recording path");
                const auto document =
                    operation == "replay"
                        ? read_input_recording(
                              files.document.project(),
                              automation_relative_path(request.at("path").get<std::string>()))
                        : Json();
                if (!play.submit_input_recording(operation, document))
                    throw AuthoringError("play_busy",
                                         "Wait for fresh paused Play or the pending input control");
                return state(play);
            },
            true));
        if (permissions.assets) {
            auto save_properties = properties();
            save_properties.update(
                {{"path", automation_text(2047, 1)}, {"replace", {{"type", "boolean"}}}});
            auto save_required = required();
            save_required.push_back("path");
            methods.push_back(automation_method(
                "play.input_recording.save",
                "Save the acknowledged input recording as Recordings/<name>.json in the project. "
                "Saving creates that folder. Existing files require explicit replacement. Requires "
                "asset "
                "write and execution grants; no Scene history change. Finish and Read before Save.",
                "assets.write", automation_object(save_properties, save_required), true,
                [&files, &play, providers](const Json& request) {
                    current(play, files, providers, request);
                    if (!play.input_recording_project(files.document.project()))
                        throw AuthoringError("stale_play",
                                             "The recording belongs to another project");
                    if (play.input_recording_pending() || play.input_recording_document().is_null())
                        throw AuthoringError("not_ready",
                                             "Read the finished recording before saving");
                    const auto path =
                        automation_relative_path(request.at("path").get<std::string>());
                    write_input_recording(files.document.project(), path,
                                          play.input_recording_document(),
                                          request.value("replace", false));
                    return Json{{"path", path_utf8(path)}, {"saved", true}};
                },
                true));
        }
        for (const auto* action : {"stop", "pause", "resume", "step"})
            methods.push_back(automation_method(
                std::string("play.") + action,
                std::string(action) +
                    " the exact observed runtime using existing physical-input release policy. "
                    "A queued control is acknowledged by later play.status timing; Stop "
                    "preserves the authored Scene.",
                "play.execute", automation_object(properties(), required()), true,
                [&files, &play, providers, action](const Json& request) {
                    current(play, files, providers, request);
                    const auto operation = std::string_view(action);
                    if (operation != "stop") {
                        if (!play.control_ready())
                            throw AuthoringError("play_busy",
                                                 "Wait for the pending runtime control");
                        if (operation == "step" && !play.paused())
                            throw AuthoringError("not_paused",
                                                 "Pause before stepping a fixed tick");
                    }
                    if (!((operation == "pause" && play.paused()) ||
                          (operation == "resume" && !play.paused())))
                        providers.control(action);
                    return state(play);
                },
                true));
        auto tune_properties = properties(), tune_required = required();
        tune_properties.update({{"entity", automation_uuid()},
                                {"component", automation_text(256, 1)},
                                {"property", automation_text(256, 1)},
                                {"value", Json::object()}});
        for (const auto* key : {"entity", "component", "property", "value"})
            tune_required.push_back(key);
        methods.push_back(automation_method(
            "play.tune",
            "Queue one reflected runtime property through the actual live-tuning "
            "owner, including an existing Mesh Renderer's complete wind value (static Mesh "
            "geometry only; authored transforms and collision remain unchanged), "
            "an admitted Camera Shot's blend_in_seconds (its camera, "
            "sequence and interval remain Edit-only), Reflection Probe scalar controls and diffuse "
            "intensity (map "
            "assignment is Edit-only), Fog Volume enabled, shape, linear RGB color, density, "
            "local half extents, layers and priority, "
            "Decal Projector appearance, volume, angle fade, layers, priority and "
            "color/normal/roughness influences (Source Material "
            "assignment is Edit-only), "
            "Light shadow source radius/angular radius, emitter radius/full width/full height, "
            "and Directional image width/height/tiling (image assignment and other Light fields "
            "remain Edit-only), "
            "Sprite appearance/ordered frame regions and Navigation Agent "
            "flags, speed, stopping distance and destination "
            "coordinates (not its NavMesh asset), and an existing State Condition's enabled, "
            "domain and state fields, and UI Values' named numeric collection with runtime "
            "semantic validation. An admitted Animator graph exposes its declared graph_input_1..4 "
            "and graph_override_1..4 fields; graph/skeleton/mode selection stays Edit-only. "
            "Poll play.status.tune_ack "
            "for runtime "
            "validation. This never "
            "changes source, component schemas or authored values.",
            "play.execute", automation_object(tune_properties, tune_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (request.at("value").dump().size() > 65536)
                    throw AuthoringError("value_limit", "A live property value exceeds64KiB");
                if (!play.submit_property_tune(request.at("entity"), request.at("component"),
                                               request.at("property"), request.at("value")))
                    throw AuthoringError("play_busy", "Live tuning is unavailable or pending");
                return state(play);
            },
            true));
        auto input_properties = properties(), input_required = required();
        input_properties["events"] = {
            {"type", "array"},
            {"minItems", 1},
            {"maxItems", 64},
            {"items", automation_object({{"control", automation_text(128, 1)},
                                         {"value", {{"type", "number"}}},
                                         {"reset", {{"type", "boolean"}}}})}};
        input_required.push_back("events");
        methods.push_back(automation_method(
            "play.input.submit",
            "Queue up to64 validated semantic input events through the actual "
            "Play input queue. Inspect registered controls with play.controls. "
            "A reset releases controls; queued input is not a fixed-tick ack.",
            "play.execute", automation_object(input_properties, input_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.ready())
                    throw AuthoringError("not_ready", "Wait for live Play before supplying input");
                const auto events = request.at("events").get<std::vector<InputEvent>>();
                try {
                    for (const auto& event : events)
                        validate_input_event(event);
                } catch (const std::exception& error) {
                    throw AuthoringError("invalid_input", error.what());
                }
                // The shared physical release may queue one neutral event.
                if (!play.input_queue_room(events.size() + 1))
                    throw AuthoringError("play_busy", "Wait for the pending input queue");
                if (!providers.prepare_input || !providers.prepare_input())
                    throw AuthoringError("input_release_failed",
                                         "Release physical game input first");
                for (const auto& event : events)
                    play.input_event(event);
                return Json{{"queued", events.size()}};
            },
            true));
        auto scale_properties = properties(), scale_required = required();
        scale_properties["value"] = {{"type", "number"}, {"minimum", 0}, {"maximum", 4}};
        scale_required.push_back("value");
        methods.push_back(automation_method(
            "play.time_scale",
            "Queue the existing runtime clock's finite [0,4] scale; does not "
            "change authored simulation settings.",
            "play.execute", automation_object(scale_properties, scale_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.set_time_scale(request.at("value")))
                    throw AuthoringError("play_busy", "Time scale is unavailable or pending");
                return state(play);
            },
            true));
        append_watch_controls(methods, files, play, providers);
        auto state_properties = properties(), state_required = required();
        for (const auto* key : {"domain", "state"}) {
            state_properties[key] = {{"type", "string"},
                                     {"minLength", 1},
                                     {"maxLength", 48},
                                     {"pattern", "^[a-z][a-z0-9_]*$"}};
            state_required.push_back(key);
        }
        state_properties["domain_revision"] = automation_integer();
        state_required.push_back("domain_revision");
        methods.push_back(automation_method(
            "play.application_state.request",
            "Queue a runtime-only state transition through the current Play owner. Read "
            "play.application_states first for domain choices and revision; pass the domain's "
            "revision as domain_revision. Requests can run "
            "while paused without advancing time. Computed, inactive, stale or unknown states "
            "are rejected; read play.status for pending and error after the reply. Authored "
            "scene and initial states are unchanged.",
            "play.execute", automation_object(state_properties, state_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.submit_application_state(request.at("domain"), request.at("state"),
                                                   request.at("domain_revision")))
                    throw AuthoringError("play_busy",
                                         "Application state control is unavailable or pending");
                return state(play);
            },
            true));
        auto scope_properties = state_properties, scope_required = state_required;
        scope_properties["entity"] = {
            {"type", "object"},
            {"properties", {{"scene", automation_uuid()}, {"entity", automation_uuid()}}},
            {"required", {"scene", "entity"}},
            {"additionalProperties", false}};
        scope_required.push_back("entity");
        methods.push_back(automation_method(
            "play.application_state.scope",
            "Bind a runtime-created entity or prefab root to a currently active state or "
            "ancestor. Use its current Scene/entity reference from the Play snapshot and "
            "domain_revision from play.application_states. Authored entities, unavailable "
            "roots, inactive states, stale revisions and rebinding are rejected. The root's "
            "structural children retire when that state exits. This transient binding has "
            "no authored Undo and survives releasing the C++ creation request observation.",
            "play.execute", automation_object(scope_properties, scope_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.submit_application_state_scope(request.at("entity").get<EntityRef>(),
                                                         request.at("domain"), request.at("state"),
                                                         request.at("domain_revision")))
                    throw AuthoringError("play_busy",
                                         "State lifetime control is unavailable or pending");
                return state(play);
            },
            true));
        append_tuning_controls(methods, scene, files, play, tuning, providers);
        for (auto& method : methods)
            method.executes_native = method.capability == "play.execute";
        return methods;
    }
    static void append_watch_controls(std::vector<LiveAuthoringMethod>& methods, EditorFiles& files,
                                      PlaySession& play, PlayAutomationProviders providers) {
        auto watch_properties = properties(), watch_required = required();
        watch_properties["entity"] = automation_uuid();
        watch_required.push_back("entity");
        methods.push_back(automation_method(
            "play.watch",
            "Select the actual Inspector entity and watch its native audio/animation "
            "state. Watch identity is explicit on later controls; human selection "
            "changes cannot silently redirect them.",
            "play.execute", automation_object(watch_properties, watch_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.ready())
                    throw AuthoringError("not_ready", "Wait for a live Play world");
                const auto entity = request.at("entity").get<std::string>();
                providers.select_entity(entity);
                play.watch_audio_source(
                    AudioSourceEditor::selected(play.effective_snapshot(), entity));
                play.watch_animation(
                    AnimationPlaybackEditor::selected(play.effective_snapshot(), entity));
                return Json{{"audio_source", play.audio_source_watch_},
                            {"animation_source", play.animation_watch_}};
            },
            true));
        for (bool animation : {false, true}) {
            auto values = properties(), keys = required();
            values["entity"] = automation_uuid();
            values["operation"] = {
                {"type", "string"},
                {"enum", animation ? Json{"play", "pause", "restart", "seek"}
                                   : Json{"play", "stop", "pause", "resume", "one_shot"}}};
            if (animation)
                values["position_seconds"] = {{"type", "number"}, {"minimum", 0}};
            keys.push_back("entity");
            keys.push_back("operation");
            methods.push_back(automation_method(
                animation ? "play.animation.control" : "play.audio.control",
                "Control the exact watched entity through its existing live owner. Watch first, "
                "then wait for an available status. Cue one_shot requests one event; configured "
                "interval/completion cues use play/pause/resume/stop for future events. Audio "
                "Sources own "
                "existing voices. Seek requires position_seconds.",
                "play.execute", automation_object(values, keys), true,
                [&files, &play, providers, animation](const Json& request) {
                    current(play, files, providers, request);
                    const auto& watch =
                        animation ? play.animation_watch_ : play.audio_source_watch_;
                    if (!watch.is_object() || watch.at("entity") != request.at("entity"))
                        throw AuthoringError("stale_watch", "Watch this exact entity first");
                    const auto operation = request.at("operation").get<std::string>();
                    bool queued = false;
                    if (animation) {
                        if ((operation == "seek") != request.contains("position_seconds"))
                            throw AuthoringError("invalid_argument",
                                                 "Only Seek requires a position");
                        Json command{{"operation", operation}};
                        if (operation == "seek")
                            command["position_seconds"] = request.at("position_seconds");
                        queued = play.submit_animation(command);
                    } else {
                        const auto op = operation == "play"     ? AudioSourceOperation::Restart
                                        : operation == "stop"   ? AudioSourceOperation::Stop
                                        : operation == "pause"  ? AudioSourceOperation::Pause
                                        : operation == "resume" ? AudioSourceOperation::Resume
                                                                : AudioSourceOperation::OneShot;
                        queued = play.submit_audio_source(op);
                    }
                    if (!queued)
                        throw AuthoringError("play_busy",
                                             "The watched source is unavailable or pending");
                    return Json{{"queued", true}};
                },
                true));
            // Repeating a one-shot/restart is another event with identical arguments.
            methods.back().idempotent = false;
        }
        auto mix_properties = properties(), mix_required = required();
        const Json volume_schema{{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
        mix_properties["values"] = automation_object(
            {{"master_volume", volume_schema},
             {"channel_volumes",
              {{"type", "array"}, {"items", volume_schema}, {"minItems", 3}, {"maxItems", 3}}},
             {"bus_volumes",
              {{"type", "object"},
               {"maxProperties", audio_custom_bus_limit},
               {"additionalProperties", volume_schema},
               {"propertyNames",
                {{"type", "string"},
                 {"maxLength", audio_bus_key_limit},
                 {"pattern", "^[a-z][a-z0-9_]*$"}}}}}},
            {"master_volume", "channel_volumes"});
        mix_required.push_back("values");
        methods.push_back(automation_method(
            "play.audio.mix",
            "Queue live Master, three category faders and optional custom bus_volumes through the "
            "existing AudioMix owner. "
            "Omitting bus_volumes preserves custom faders; when present its keys must exactly "
            "match the admitted project graph. "
            "Topology and project defaults remain unchanged.",
            "play.execute", automation_object(mix_properties, mix_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.submit_audio_mix(request.at("values")))
                    throw AuthoringError("play_busy", "Audio mix is unavailable or pending");
                return Json{{"queued", true}};
            },
            true));
        auto profile_properties = properties(), profile_required = required();
        profile_properties.update({{"enabled", {{"type", "boolean"}}},
                                   {"clear", {{"type", "boolean"}}},
                                   {"native_systems", {{"type", "boolean"}}}});
        profile_required.push_back("enabled");
        profile_required.push_back("clear");
        methods.push_back(automation_method(
            "play.profile.control",
            "Control the existing runtime CPU collection. Read a fresh "
            "snapshot with play.profile.refresh after it is acknowledged. Optional "
            "native_systems captures actual native dispatch intervals on the caller and "
            "supported worker stages. Custom clocks/external worker configurations retain "
            "owner-only capture. This is wall-time observation, not hardware CPU utilization "
            "or native lifetime totals.",
            "play.execute", automation_object(profile_properties, profile_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                const auto native_systems =
                    request.contains("native_systems")
                        ? std::optional<bool>(request.at("native_systems").get<bool>())
                        : std::nullopt;
                if (native_systems.value_or(false) &&
                    !play.cpu_profile().value("native_systems_available", false))
                    throw AuthoringError("not_supported", "Refresh the profile and use a Play "
                                                          "host with native system tracing");
                if (!play.submit_cpu_profile(request.at("enabled"), request.at("clear"),
                                             native_systems))
                    throw AuthoringError("play_busy",
                                         "CPU profiling control is pending or unavailable");
                return Json{{"queued", true}};
            },
            true));
        methods.push_back(automation_method(
            "play.profile.refresh",
            "Request one CPU profile snapshot, including the last collected native ECS "
            "plan and cumulative system CPU totals, without changing the Profiler window's "
            "watch settings. Start collection and advance a fixed tick for ECS data. "
            "Poll play.profile; Clear discards its plan, not Flecs lifetime counters. With "
            "multiple fixed ECS workers, per-system CPU totals are unavailable (null) and "
            "system_trace_scope identifies supported native stage or owner-only capture; "
            "fixed-tick scope timing remains. Trace records identify actually observed stages. "
            "The copied plan includes bounded declared query access and possible component "
            "overlaps in native order, with unresolved/truncated coverage. These are not "
            "measured races or complete ambiguity analysis. Bounded native DependsOn lookups "
            "include target slots, phase tags and owned-pair presence; lookup may resolve "
            "inherited targets. The legacy phase field names the first target, not a resolved "
            "execution phase. Links are not a complete ordering graph. Bounded worker_access "
            "references identify declared fixed-source writes in multi-threaded This-source "
            "systems with multiple stages. Counts and unresolved/truncated coverage accompany "
            "the declarations; they do not measure actual writes, races or concurrency.",
            "play.execute", automation_object(properties(), required()), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.request_cpu_profile_snapshot())
                    throw AuthoringError("play_busy", "CPU snapshot is pending or unavailable");
                return Json{{"queued", true}};
            },
            true));
        methods.push_back(automation_method(
            "play.ui.reload",
            "Validate current saved RML/RCSS/resources and reload through the live presenter. "
            "Failure retains the previous usable UI. Releases UI input and restarts native UI "
            "animations on success; gameplay and authored Scene/history remain unchanged.",
            "play.execute", automation_object(properties(), required()), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!providers.reload_ui)
                    throw AuthoringError("ui_unavailable", "Live UI reload is unavailable");
                if (!play.ready())
                    throw AuthoringError("play_busy", "Wait for the current Play world");
                providers.reload_ui();
                return Json{{"reloaded", true}};
            },
            true));
        auto ui_properties = properties(), ui_required = required();
        ui_properties.update({{"instance", automation_text(256, 1)},
                              {"command", automation_text(128, 1)},
                              {"value", automation_text(ui_protocol::max_string)}});
        ui_required.push_back("instance");
        ui_required.push_back("command");
        methods.push_back(automation_method(
            "play.ui.command",
            "Queue a declared runtime UI command through the actual presenter "
            "and its command-ID/acknowledgement queue. Inspect play.ui for the "
            "current instance and allowed commands; this does not inject DOM/script.",
            "play.execute", automation_object(ui_properties, ui_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                std::optional<std::string> value;
                if (request.contains("value"))
                    value = request.at("value").get<std::string>();
                if (!providers.ui_command ||
                    !providers.ui_command(request.at("instance"), request.at("command"), value))
                    throw AuthoringError("ui_unavailable",
                                         "The live presenter rejected this UI command");
                return Json{{"queued", true}};
            },
            true));
        auto field_properties = properties(), field_required = required();
        field_properties.update({{"instance", automation_text(256, 1)},
                                 {"element", automation_text(256, 1)},
                                 {"presentation_revision", automation_integer()}});
        for (const auto* name : {"instance", "element", "presentation_revision"})
            field_required.push_back(name);
        Json keys = Json::array();
        for (const auto& key : ui_field::keys)
            keys.push_back(key.name);
        for (const auto* action : {"focus", "text", "key"}) {
            auto properties = field_properties, required = field_required;
            const std::string kind = action;
            if (kind == "text") {
                properties["text"] = automation_text(ui_protocol::max_string);
                required.push_back("text");
            } else if (kind == "key") {
                properties["key"] = {{"type", "string"}, {"enum", keys}};
                required.push_back("key");
            }
            methods.push_back(automation_method(
                "play.ui." + kind,
                (kind == "focus" ? "Focus the exact native text/password/textarea field. "
                 : kind == "text"
                     ? "Send UTF-8 text through the exact focused native field. "
                     : "Send one paired native key through the exact focused field. "
                       "select_left/select_right send Shift+Left/Right for native selection. "
                       "word_left/right send Ctrl arrows; select_word_left/right add Shift. "
                       "up/down and page_up/down retain native layout; select_ variants add Shift. "
                       "select_home/end select to native line bounds; document_home/end add Ctrl. "
                       "select_document_home/end add Ctrl+Shift. word_backspace/delete send native "
                       "Ctrl+Backspace/Delete; a collapsed word range can split a combined "
                       "character. The whole-character option also aligns observed native "
                       "line/page/vertical destinations and existing modified deletion "
                       "selections. ") +
                    std::string(
                        "Read play.ui.fields first and supply its presentation revision, "
                        "document instance and unique element ID. Native values, selection, "
                        "events, "
                        "disabled/maximum-length rules and the saved whole-character option apply. "
                        "Camera-output input requires an existing current native surface hit. "
                        "Release held UI keys/pointer buttons first. Text/key requires this exact "
                        "field still to have focus. Returns updated field observations and "
                        "delivered/"
                        "consumed; delivered is not a gameplay acknowledgement. Transient UI only: "
                        "no RML/Scene/history write, no arbitrary DOM/script or IME composition "
                        "injection."),
                "play.execute", automation_object(properties, required), true,
                [&files, &play, providers, kind](const Json& request) {
                    current(play, files, providers, request);
                    if (!play.ready())
                        throw AuthoringError("not_ready", "Wait for the current Play world");
                    if (!providers.ui_field_action || !providers.ui_fields)
                        throw AuthoringError("ui_unavailable",
                                             "Native UI field input is unavailable");
                    const auto shown = providers.ui_fields(request.at("instance"));
                    if (!shown.is_object() || shown.at("session") != play.session() ||
                        shown.at("generation") != play.world_generation() ||
                        shown.at("presentation_revision") != request.at("presentation_revision"))
                        throw AuthoringError("stale_ui", "Read the current native UI fields first");
                    return providers.ui_field_action(
                        request.at("instance"), request.at("element"),
                        request.at("presentation_revision"),
                        kind == "key" ? request.at("key").get<std::string>() : kind,
                        kind == "text" ? request.at("text").get<std::string>() : std::string{});
                },
                true));
        }
        for (const auto* name : {"check", "choose", "select"}) {
            const std::string action(name);
            auto properties = field_properties, required = field_required;
            if (action == "check") {
                properties["checked"] = {{"type", "boolean"}};
                required.push_back("checked");
            } else if (action == "select") {
                properties["index"] = automation_integer(0, ui_control_nodes - 1);
                properties["option_state"] = automation_digest();
                required.push_back("index");
                required.push_back("option_state");
            }
            methods.push_back(automation_method(
                "play.ui." + action,
                "Apply one semantic native checkbox check(checked), radio choose, or dropdown "
                "select(index/option_state from play.ui.options). Needs current Play scope, "
                "instance/unique element/presentation revision and play.execute. Native Click/"
                "SetSelection owns state, groups and change events; repeated desired state is "
                "an eventless no-op. Validate visible/enabled kind, IDs and option state again "
                "after native focus callbacks. Held UI input rejects; camera outputs require a "
                "current native surface hit. Returns actual form observations, delivered and "
                "changed(null if target retired). Delivered means native activation/selection, "
                "not gameplay acknowledgement. Transient only; no source/Scene/history writes, "
                "arbitrary DOM operations, password actions or progress value replacement.",
                "play.execute", automation_object(properties, required), true,
                [&files, &play, providers, action](const Json& request) {
                    current(play, files, providers, request);
                    if (!play.ready())
                        throw AuthoringError("not_ready", "Wait for the current Play world");
                    if (!providers.ui_control_action || !providers.ui_controls ||
                        (action == "select" && !providers.ui_options))
                        throw AuthoringError("ui_unavailable",
                                             "Native UI control input is unavailable");
                    const auto shown = providers.ui_controls(request.at("instance"));
                    if (!shown.is_object() || shown.at("session") != play.session() ||
                        shown.at("generation") != play.world_generation() ||
                        shown.at("presentation_revision") != request.at("presentation_revision"))
                        throw AuthoringError("stale_ui",
                                             "Read the current native UI controls first");
                    if (action == "select") {
                        const auto page =
                            providers.ui_options(request.at("instance"), request.at("element"),
                                                 request.at("index").get<std::size_t>(), 1);
                        if (!page.is_object() || page.at("session") != play.session() ||
                            page.at("generation") != play.world_generation() ||
                            page.at("presentation_revision") != request.at("presentation_revision"))
                            throw AuthoringError("stale_ui",
                                                 "Read the current dropdown options first");
                        if (!page.at("ready").get<bool>() || page.at("options").size() != 1 ||
                            page.at("options")[0].at("state") != request.at("option_state"))
                            throw AuthoringError("stale_ui_option",
                                                 "Read the current dropdown option page first");
                    }
                    return providers.ui_control_action(
                        request.at("instance"), request.at("element"),
                        request.at("presentation_revision"), action,
                        request.value("checked", false), request.value("index", std::size_t{}),
                        request.value("option_state", std::string{}));
                },
                true));
        }
        auto range_properties = field_properties;
        range_properties["value"] = {
            {"type", "number"}, {"minimum", -ui_range_magnitude}, {"maximum", ui_range_magnitude}};
        auto range_required = field_required;
        range_required.push_back("value");
        methods.push_back(automation_method(
            "play.ui.range_value",
            "Set one native slider through current Play/session/world, instance, unique ID and "
            "presentation revision. Finite value/min/max magnitudes <=1000000 and step >=0.000001 "
            "are required. Revalidate native type/configuration after focus. Native SetValue owns "
            "clamping, quantization and Change; repeated out-of-range requests may emit Change "
            "even when returned value text stays equal. Returns delivered:true and nullable "
            "value_text_changed plus fresh observations, not gameplay acknowledgement or private "
            "float change. Each fresh request delivers; receipt replay never sets twice. Held UI "
            "input rejects and camera outputs require a current surface hit. No arbitrary DOM, "
            "password, source/Scene/history mutation; native callback lifetime rules apply.",
            "play.execute", automation_object(range_properties, range_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.ready())
                    throw AuthoringError("not_ready", "Wait for the current Play world");
                if (!providers.ui_ranges || !providers.ui_range_value)
                    throw AuthoringError("ui_unavailable", "Native slider input is unavailable");
                const auto shown = providers.ui_ranges(request.at("instance"));
                if (!shown.is_object() || shown.at("session") != play.session() ||
                    shown.at("generation") != play.world_generation() ||
                    shown.at("presentation_revision") != request.at("presentation_revision"))
                    throw AuthoringError("stale_ui", "Read the current native sliders first");
                return providers.ui_range_value(request.at("instance"), request.at("element"),
                                                request.at("presentation_revision"),
                                                request.at("value"));
            },
            true));
        methods.push_back(automation_method(
            "play.ui.press",
            "Activate one exact native button through current Play scope and "
            "instance/unique element/presentation revision. Native focus and Click own events; "
            "validate kind, visibility, enabled/focus eligibility and identity again after focus "
            "callbacks. Held UI input rejects and camera outputs require a current surface hit. "
            "Returns delivered:true when Click was invoked and fresh button observations, not "
            "a gameplay acknowledgement. Each fresh request can activate; receipt replay never "
            "clicks twice. Generic button enabled is unavailable; focus:none blocks activation. "
            "Input type=button supports native disabled. No arbitrary DOM/script, submit/reset, "
            "password access or source/Scene/history writes; native callback lifetime rules apply.",
            "play.execute", automation_object(field_properties, field_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.ready())
                    throw AuthoringError("not_ready", "Wait for the current Play world");
                if (!providers.ui_buttons || !providers.ui_press)
                    throw AuthoringError("ui_unavailable", "Native button input is unavailable");
                const auto shown = providers.ui_buttons(request.at("instance"));
                if (!shown.is_object() || shown.at("session") != play.session() ||
                    shown.at("generation") != play.world_generation() ||
                    shown.at("presentation_revision") != request.at("presentation_revision"))
                    throw AuthoringError("stale_ui", "Read the current native buttons first");
                return providers.ui_press(request.at("instance"), request.at("element"),
                                          request.at("presentation_revision"));
            },
            true));
    }
    static void append_tuning_controls(std::vector<LiveAuthoringMethod>& methods, Scene& scene,
                                       EditorFiles& files, PlaySession& play,
                                       RuntimeTuningReview& tuning,
                                       PlayAutomationProviders providers) {
        auto values = properties(), keys = required();
        values["entity"] = automation_uuid();
        keys.push_back("entity");
        methods.push_back(automation_method(
            "play.tuning.capture",
            "Freeze tunable live values in the existing Keep runtime values "
            "review. Stop, review/select fields, then Apply separately.",
            "play.execute", automation_object(values, keys), true,
            [&scene, &files, &play, &tuning, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.ready() ||
                    !tuning.capture(scene, files.document, play.effective_snapshot(),
                                    play.runtime_schema(), request.at("entity"),
                                    play.timing().value("tick", std::uint64_t{})))
                    throw AuthoringError("review_failed", tuning.status());
                tuning.open = false;
                return review(tuning);
            },
            true));
        for (const auto* action : {"select", "apply", "discard"}) {
            Json fields{{"state", automation_digest()}};
            Json required_fields{"state"};
            if (std::string_view(action) == "select") {
                fields["indices"] = {
                    {"type", "array"}, {"maxItems", 128}, {"items", automation_integer(0, 127)}};
                required_fields.push_back("indices");
            }
            methods.push_back(automation_method(
                std::string("play.tuning.") + action,
                std::string(action) +
                    " the exact frozen live-value review through its existing "
                    "owner. Select supplies the complete set of kept field indices. "
                    "Apply requires Stop and creates one ordinary Scene Undo step.",
                "play.execute", automation_object(fields, required_fields), true,
                [&scene, &files, &play, &tuning, action](const Json& request) {
                    files.document.check_ownership();
                    if (!tuning.pending() || review(tuning).at("state") != request.at("state"))
                        throw AuthoringError("stale_review",
                                             "Read the current live-value review first");
                    const auto operation = std::string_view(action);
                    if (operation == "select") {
                        const auto indices = request.at("indices").get<std::vector<std::size_t>>();
                        for (const auto i : indices)
                            if (i >= tuning.fields().size())
                                throw AuthoringError("invalid_index",
                                                     "Choose an advertised review field");
                        for (std::size_t i = 0; i < tuning.fields().size(); ++i)
                            tuning.select(i, std::find(indices.begin(), indices.end(), i) !=
                                                 indices.end());
                    } else if (operation == "discard")
                        tuning.discard();
                    else if (!tuning.apply(scene, files.document, play.active()))
                        throw AuthoringError("review_failed", tuning.status());
                    return review(tuning);
                }));
        }
    }
};
} // namespace forge
