#pragma once
#include "runtime_document_package.hpp"
#include "standalone_manifest.hpp"
#include <forge/audio_components.hpp>

template <class Check, class Reject>
void sound_cue_package_tests(const std::filesystem::path& scratch, Check check, Reject reject) {
    using namespace forge;
    using Json = nlohmann::json;
    for (const char* rows : {"entities", "members", "overrides"})
        for (const char* channel : {"components", "property_overrides"}) {
            Json document{
                {rows, Json::array({{{channel, {{"forge.sound_cue", Json::object()}}}}})}};
            if (std::string_view(rows) == "overrides")
                document["base"] = Json::object();
            const auto features = package_detail::document_runtime_features(document);
            check(features == Json({{"sound_cues", sound_cue_profile_version}}),
                  "Partial/retained Sound Cue escaped runtime capability discovery");
            AssetRecord record{};
            record.id = AssetId::generate();
            record.type = "scene";
            record.source = "Cue.scene.json";
            record.schema_version = 1;
            record.metadata["forge.runtime_document"]["runtime_features"] = features;
            AssetCatalog catalog(scratch / (std::string("sound-cue-") + rows + channel));
            catalog.add(record);
            reject([&] { validate_runtime_compatibility(catalog, Json::object()); });
            validate_runtime_compatibility(catalog,
                                           {{"runtime_sound_cues", sound_cue_profile_version}});
            for (const auto& invalid :
                 {Json(false), Json(0), Json(1.), Json("1"), Json(2), Json(std::uint64_t{1} << 32)})
                reject([&] {
                    validate_runtime_compatibility(catalog, {{"runtime_sound_cues", invalid}});
                });
            auto& graph = document[rows][0][channel]["forge.sound_cue"];
            graph["repeat_event_limit"] = 2;
            const auto finite = package_detail::document_runtime_features(document);
            check(finite == Json({{"sound_cues", sound_cue_profile_version},
                                  {"sound_cue_repeat", sound_cue_repeat_profile_version},
                                  {"sound_cue_finite", sound_cue_finite_profile_version}}),
                  "Retained finite limit escaped capability discovery");
            AssetCatalog finite_catalog(scratch / (std::string("finite-cue-") + rows + channel));
            record.metadata["forge.runtime_document"]["runtime_features"] = finite;
            finite_catalog.add(record);
            Json finite_kit{{"runtime_sound_cues", sound_cue_profile_version},
                            {"runtime_sound_cue_repeat", sound_cue_repeat_profile_version}};
            reject([&] { validate_runtime_compatibility(finite_catalog, finite_kit); });
            finite_kit["runtime_sound_cue_finite"] = sound_cue_finite_profile_version;
            validate_runtime_compatibility(finite_catalog, finite_kit);
            for (const Json& invalid : {Json(false), Json(0), Json(1.), Json("1"), Json(2)}) {
                finite_kit["runtime_sound_cue_finite"] = invalid;
                reject([&] { validate_runtime_compatibility(finite_catalog, finite_kit); });
            }
            for (const Json& invalid : {Json(false), Json(-1), Json(1.), Json("2"), Json(65536),
                                        Json(std::uint64_t{1} << 40)}) {
                graph["repeat_event_limit"] = invalid;
                reject([&] { (void)package_detail::document_runtime_features(document); });
            }
            graph["repeat_event_limit"] = 0;
            check(package_detail::document_runtime_features(document) == features,
                  "Unlimited cue unnecessarily required finite playback support");
            graph.erase("repeat_event_limit");
            graph["repeat_mode"] = 1;
            const auto completed = package_detail::document_runtime_features(document);
            check(completed ==
                      Json({{"sound_cues", sound_cue_profile_version},
                            {"sound_cue_repeat", sound_cue_repeat_profile_version},
                            {"sound_cue_completion", sound_cue_completion_profile_version}}),
                  "Retained zero-gap completion mode escaped its kit guard");
            AssetCatalog completion_catalog(scratch /
                                            (std::string("completion-cue-") + rows + channel));
            record.metadata["forge.runtime_document"]["runtime_features"] = completed;
            completion_catalog.add(record);
            reject([&] {
                validate_runtime_compatibility(
                    completion_catalog,
                    {{"runtime_sound_cues", sound_cue_profile_version},
                     {"runtime_sound_cue_repeat", sound_cue_repeat_profile_version}});
            });
            validate_runtime_compatibility(
                completion_catalog,
                {{"runtime_sound_cues", sound_cue_profile_version},
                 {"runtime_sound_cue_repeat", sound_cue_repeat_profile_version},
                 {"runtime_sound_cue_completion", sound_cue_completion_profile_version}});
            for (const auto& invalid : {Json(false), Json(0), Json(1.), Json("1"), Json(2)})
                reject([&] {
                    validate_runtime_compatibility(
                        completion_catalog,
                        {{"runtime_sound_cues", sound_cue_profile_version},
                         {"runtime_sound_cue_repeat", sound_cue_repeat_profile_version},
                         {"runtime_sound_cue_completion", invalid}});
                });
            for (const auto& invalid :
                 {Json(false), Json(-1), Json(1.), Json("1"), Json(2), Json(nullptr)}) {
                graph["repeat_mode"] = invalid;
                reject([&] { (void)package_detail::document_runtime_features(document); });
            }
            graph["repeat_mode"] = 0;
            check(package_detail::document_runtime_features(document) == features,
                  "Default interval mode added a completion kit requirement");
            graph.erase("repeat_mode");
            graph["nodes"] = Json::array({{{"delay_seconds", .25}}});
            const auto timed = package_detail::document_runtime_features(document);
            check(timed == Json({{"sound_cues", sound_cue_profile_version},
                                 {"sound_cue_timing", sound_cue_timing_profile_version}}),
                  "Partial timed cue escaped its additional runtime guard");
            AssetCatalog timed_catalog(scratch / (std::string("timed-cue-") + rows + channel));
            record.metadata["forge.runtime_document"]["runtime_features"] = timed;
            timed_catalog.add(record);
            reject([&] {
                validate_runtime_compatibility(timed_catalog,
                                               {{"runtime_sound_cues", sound_cue_profile_version}});
            });
            validate_runtime_compatibility(
                timed_catalog, {{"runtime_sound_cues", sound_cue_profile_version},
                                {"runtime_sound_cue_timing", sound_cue_timing_profile_version}});
            for (const auto& invalid : {Json(false), Json(0), Json(1.), Json("1"), Json(2)})
                reject([&] {
                    validate_runtime_compatibility(
                        timed_catalog, {{"runtime_sound_cues", sound_cue_profile_version},
                                        {"runtime_sound_cue_timing", invalid}});
                });
            graph["nodes"][0]["delay_seconds"] = 0.;
            check(package_detail::document_runtime_features(document) == features,
                  "Zero delay unnecessarily changed earlier cue kit requirements");
            graph["nodes"][0]["delay_seconds"] = nullptr;
            reject([&] { (void)package_detail::document_runtime_features(document); });
            for (const char* field :
                 {"repeat_interval_seconds", "repeat_on_start", "repeat_pause_with_game"}) {
                graph = Json::object();
                graph[field] = std::string_view(field) == "repeat_interval_seconds"
                                   ? Json(.5)
                                   : Json(std::string_view(field) == "repeat_on_start");
                const auto repeated = package_detail::document_runtime_features(document);
                check(repeated == Json({{"sound_cues", sound_cue_profile_version},
                                        {"sound_cue_repeat", sound_cue_repeat_profile_version}}),
                      "Retained partial repetition intent escaped its runtime guard");
                record.metadata["forge.runtime_document"]["runtime_features"] = repeated;
                AssetCatalog repeat_catalog(
                    scratch / (std::string("repeated-cue-") + rows + channel + field));
                repeat_catalog.add(record);
                reject([&] {
                    validate_runtime_compatibility(
                        repeat_catalog, {{"runtime_sound_cues", sound_cue_profile_version}});
                });
                validate_runtime_compatibility(
                    repeat_catalog,
                    {{"runtime_sound_cues", sound_cue_profile_version},
                     {"runtime_sound_cue_repeat", sound_cue_repeat_profile_version}});
                for (auto invalid : {Json(false), Json(0), Json(1.), Json("1"), Json(2)})
                    reject([&] {
                        validate_runtime_compatibility(
                            repeat_catalog, {{"runtime_sound_cues", sound_cue_profile_version},
                                             {"runtime_sound_cue_repeat", invalid}});
                    });
                graph[field] = nullptr;
                reject([&] { (void)package_detail::document_runtime_features(document); });
            }
            document[rows][0][channel]["forge.sound_cue"] = nullptr;
            check(package_detail::document_runtime_features(document).empty(),
                  "Removed Sound Cue unnecessarily required a new runtime profile");
        }
}
