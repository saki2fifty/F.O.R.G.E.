#pragma once
#include "../asset_bytes.hpp"
#include "../cpu_profile_trace.hpp"
#include "animation_playback.hpp"
#include "audio_source.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "event_time.hpp"
#include "files.hpp"
#include "play.hpp"
#include "runtime_tuning_review.hpp"

namespace forge {
struct PlayAutomationProviders {
    // Shared editor policies include asset readiness, source freshness, physical
    // input release and presentation cleanup. No second launch/input owner.
    std::function<void()> start;
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
                {"sdk", play.sdk_play()},
                {"loading", play.sdk_loading()},
                {"tune_pending", play.property_tune_pending()},
                {"tune_ack", play.property_tune_ack()}};
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
            "for a stable inspection. This never reads an authored-world copy.",
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
                result["entities"] = Json::array();
                const auto& entities = snapshot.at("entities");
                const auto offset = request.value("offset", std::size_t{}),
                           limit = request.value("limit", std::size_t(128));
                result["total"] = entities.size();
                for (auto i = offset; i < entities.size() && i - offset < limit; ++i)
                    result["entities"].push_back(entities[i]);
                return result;
            }));
        for (const auto* kind :
             {"schema", "diagnostics", "physics", "input", "ui", "audio", "animation", "profile"})
            methods.push_back(automation_method(
                std::string("play.") + kind,
                std::string("Read cached ") + kind +
                    " observations published by the actual Play owner, correlated with its "
                    "launch/session/world. Watch-dependent values require an explicit watch.",
                "play.read", automation_object(), false, [&files, &play, kind](const Json&) {
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
                    else if (key == "ui") {
                        result["value"] = play.ui_snapshot();
                        result["ack"] = play.ui_ack();
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
            "lifetime records are not given invented timestamps. Logical overlap lanes are "
            "one owner thread, not measured workers. Editor-wide traces remain in its UI.",
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
            "logical slots 1..4 and topology revision. Opaque connection tokens are not saved "
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
            "Assign a current connection to slot 1..4, or 0 to "
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
            "play.execute", automation_object(), true, [&files, &play, providers](const Json&) {
                files.document.check_ownership();
                if (play.active())
                    throw AuthoringError("play_busy", "Stop the current runtime first");
                providers.start();
                if (!play.active())
                    throw AuthoringError("not_ready", "The editor did not start a runtime");
                return state(play);
            }));
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
            "owner, including Reflection Probe scalar controls and diffuse intensity (map "
            "assignment is Edit-only), "
            "Decal Projector appearance, volume, angle fade, layers, priority and "
            "color/normal/roughness influences (Source Material "
            "assignment is Edit-only), "
            "Light shadow source radius/angular radius, emitter radius/full width/full height, "
            "and Directional image width/height/tiling (image assignment and other Light fields "
            "remain Edit-only), "
            "Sprite appearance/ordered frame regions and Navigation Agent "
            "flags, speed, stopping distance and destination "
            "coordinates (not its NavMesh asset). Poll play.status.tune_ack for runtime "
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
                "then wait for an available status. Seek requires position_seconds.",
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
        }
        auto mix_properties = properties(), mix_required = required();
        mix_properties["values"] = {{"type", "object"}};
        mix_required.push_back("values");
        methods.push_back(automation_method(
            "play.audio.mix",
            "Queue validated live mix values through the existing AudioMix owner.", "play.execute",
            automation_object(mix_properties, mix_required), true,
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
            "native_systems captures actual owner-thread native ECS dispatch scopes; "
            "it is not measured worker utilization or native lifetime totals.",
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
            "Poll play.profile; Clear discards its plan, not Flecs lifetime counters.",
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
                              {"value", automation_text(4096)}});
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
