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
            document[rows][0][channel]["forge.sound_cue"] = nullptr;
            check(package_detail::document_runtime_features(document).empty(),
                  "Removed Sound Cue unnecessarily required a new runtime profile");
        }
}
