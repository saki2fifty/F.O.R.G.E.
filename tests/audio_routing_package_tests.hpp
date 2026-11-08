#pragma once
#include "runtime_document_package.hpp"
#include "standalone_manifest.hpp"
#include <forge/game_settings.hpp>

template <class Check, class Reject>
void audio_routing_package_tests(const std::filesystem::path& scratch, Check check, Reject reject) {
    using namespace forge;
    using Json = nlohmann::json;
    AssetCatalog catalog(scratch / "audio-routing-compatibility");
    Json project{{"game", default_game_settings("org.forge.routing", "Routing")}};
    validate_runtime_compatibility(catalog, Json::object(), &project);
    project["game"]["audio"]["routing"] = audio_routing_json({{{"rain", "", 1, "effects", {}}}});
    reject([&] { validate_runtime_compatibility(catalog, Json::object(), &project); });
    const Json matching{{"runtime_audio_routing", audio_routing_profile_version}};
    validate_runtime_compatibility(catalog, matching, &project);
    auto processed = project;
    processed["game"]["audio"]["routing"]["buses"][0]["effects"] = {{"echo_enabled", true}};
    reject([&] { validate_runtime_compatibility(catalog, matching, &processed); });
    auto effect_kit = matching;
    effect_kit["runtime_audio_bus_effects"] = audio_bus_effects_profile_version;
    validate_runtime_compatibility(catalog, effect_kit, &processed);
    const auto impulse_clip = AssetId::parse("ac7f2d31-de35-4ce8-bef4-e514f21449d7");
    auto impulse_project = processed;
    impulse_project["game"]["audio"]["routing"]["buses"][0]["effects"]["impulse_response"] = {
        {"clip", impulse_clip}, {"wet", 0}};
    reject([&] { validate_runtime_compatibility(catalog, effect_kit, &impulse_project); });
    auto impulse_kit = effect_kit;
    impulse_kit["runtime_audio_bus_impulse"] = audio_bus_impulse_profile_version;
    validate_runtime_compatibility(catalog, impulse_kit, &impulse_project);
    for (const Json& invalid : {Json(), Json(false), Json(-1), Json(0), Json(1.), Json("1"),
                                Json(2), Json(std::uint64_t{1} << 32)}) {
        impulse_kit["runtime_audio_bus_impulse"] = invalid;
        reject([&] { validate_runtime_compatibility(catalog, impulse_kit, &impulse_project); });
    }
    const auto references =
        audio_bus_impulse_assets(audio_routing_settings(impulse_project.at("game").at("audio")));
    check(references == std::vector<AssetRef<AudioClipAsset>>{{impulse_clip}},
          "Bypassed impulse lost its retained typed asset dependency");
    for (const Json& invalid :
         {Json(), Json(false), Json(-1), Json(0), Json(1.), Json("1"), Json(2)}) {
        effect_kit["runtime_audio_bus_effects"] = invalid;
        reject([&] { validate_runtime_compatibility(catalog, effect_kit, &processed); });
    }
    // Dormant settings still need a runtime that understands their saved values.
    processed["game"]["audio"]["routing"]["buses"][0]["effects"] = {{"echo_seconds", .5}};
    reject([&] { validate_runtime_compatibility(catalog, matching, &processed); });
    processed["game"]["audio"]["routing"]["buses"][0]["effects"] = Json::object();
    // Older strict routing readers reject the field even when it is empty/default.
    reject([&] { validate_runtime_compatibility(catalog, matching, &processed); });
    effect_kit["runtime_audio_bus_effects"] = audio_bus_effects_profile_version;
    validate_runtime_compatibility(catalog, effect_kit, &processed);
    processed["game"]["audio"]["routing"]["buses"][0]["effects"] =
        audio_bus_effects_json(AudioBusEffects{});
    reject([&] { validate_runtime_compatibility(catalog, matching, &processed); });
    validate_runtime_compatibility(catalog, effect_kit, &processed);
    for (const Json& invalid : {Json(), Json(false), Json(-1), Json(0), Json(1.), Json("1"),
                                Json(2), Json(std::uint64_t{1} << 32)})
        reject([&] {
            validate_runtime_compatibility(catalog, {{"runtime_audio_routing", invalid}}, &project);
        });
    project["game"]["audio"]["routing"]["buses"] = Json::array();
    validate_runtime_compatibility(catalog, Json::object(), &project);

    // Detect retained selections even with no bound clip or complete component value.
    for (const char* rows : {"entities", "members", "overrides"})
        for (const char* channel : {"components", "property_overrides"}) {
            Json document{
                {rows,
                 Json::array({{{channel, {{"forge.audio_source", {{"custom_bus", "rain"}}}}}}})}};
            if (std::string_view(rows) == "overrides")
                document["base"] = Json::object();
            const auto features = package_detail::document_runtime_features(document);
            check(features == Json({{"audio_routing", 1}}),
                  "Partial authored route escaped the runtime-kit guard");
            AssetRecord record{};
            record.id = AssetId::generate();
            record.type = "scene";
            record.source = "Routing.scene.json";
            record.schema_version = 1;
            record.metadata["forge.runtime_document"]["runtime_features"] = features;
            AssetCatalog retained(scratch / (std::string(rows) + "-" + channel));
            retained.add(record);
            reject([&] { validate_runtime_compatibility(retained, Json::object()); });
            validate_runtime_compatibility(retained, matching);
            document[rows][0][channel]["forge.audio_source"]["custom_bus"] = "";
            check(package_detail::document_runtime_features(document).empty(),
                  "Empty stock selection unnecessarily required routing support");
            document[rows][0][channel]["forge.audio_source"]["custom_bus"] = 1;
            reject([&] { package_detail::document_runtime_features(document); });
        }
}
