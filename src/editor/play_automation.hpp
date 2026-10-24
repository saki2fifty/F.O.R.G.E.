#pragma once
#include "../asset_bytes.hpp"
#include "animation_playback.hpp"
#include "audio_source.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
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
    std::function<bool(const std::string&, const std::string&, const std::optional<std::string>&)>
        ui_command;
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
            "live-tuning acknowledgement. No authored data changes during Play.",
            "play.read", automation_object(), false, [&files, &play](const Json&) {
                files.document.check_ownership();
                return state(play);
            }));
        methods.push_back(automation_method(
            "play.snapshot",
            "Page actual effective runtime entities at the reported snapshot "
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
            "play.log", "Read the existing timestamped runtime log, bounded to its native log cap.",
            "play.read", automation_object(), false, [&files, &play](const Json&) {
                files.document.check_ownership();
                return Json{{"launch", play.launch_id()},
                            {"session", play.session()},
                            {"text", play.timed_log()}};
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
            "play.controls", "List the engine's actual semantic input controls and value kinds.",
            "play.read", automation_object(), false, [&files](const Json&) {
                files.document.check_ownership();
                Json controls = Json::array();
                for (const auto& control : input_controls())
                    controls.push_back({{"id", control.id},
                                        {"relative", control.relative},
                                        {"digital", control.digital}});
                return Json{{"controls", std::move(controls)}};
            }));
        if (!permissions.execute)
            return methods;
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
            "owner. Poll play.status.tune_ack for runtime validation. This never "
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
                for (const auto& event : events) {
                    if (event.reset)
                        continue;
                    const auto& control = input_control(event.control);
                    if (!std::isfinite(event.value) ||
                        std::abs(event.value) > (control.relative ? 1000000 : 1) ||
                        (control.digital && event.value != 0 && event.value != 1) ||
                        (event.control.ends_with("_trigger") && event.value < 0))
                        throw AuthoringError("invalid_input", "Invalid semantic input value");
                }
                // The shared physical release may queue one neutral event.
                if (play.input_events_.size() + events.size() + 1 > 4096)
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
        profile_properties.update(
            {{"enabled", {{"type", "boolean"}}}, {"clear", {{"type", "boolean"}}}});
        profile_required.push_back("enabled");
        profile_required.push_back("clear");
        methods.push_back(automation_method(
            "play.profile.control",
            "Control the existing runtime CPU collection. Read a fresh "
            "snapshot with play.profile.refresh after it is acknowledged.",
            "play.execute", automation_object(profile_properties, profile_required), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.submit_cpu_profile(request.at("enabled"), request.at("clear")))
                    throw AuthoringError("play_busy",
                                         "CPU profiling control is pending or unavailable");
                return Json{{"queued", true}};
            },
            true));
        methods.push_back(automation_method(
            "play.profile.refresh",
            "Request one native CPU profile snapshot without changing the "
            "Profiler window's watch settings. Poll play.profile.",
            "play.execute", automation_object(properties(), required()), true,
            [&files, &play, providers](const Json& request) {
                current(play, files, providers, request);
                if (!play.request_cpu_profile_snapshot())
                    throw AuthoringError("play_busy", "CPU snapshot is pending or unavailable");
                return Json{{"queued", true}};
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
