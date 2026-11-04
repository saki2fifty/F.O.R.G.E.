#pragma once
#include "runtime_document_package.hpp"
#include "standalone_manifest.hpp"
#include <forge/fog_volume.hpp>
namespace test_fog_package {
using Json = nlohmann::json;
template <class Check, class Reject>
void exercise(const std::filesystem::path& root, Check check, Reject reject) {
    using namespace forge;
    using package_detail::document_runtime_features;
    const Json required{{"fog_volumes", fog_volume_profile_version}};
    // Presence, including disabled intent, requires a reader that knows the type.
    for (const auto& values : {Json::object(), Json{{"enabled", false}}, Json{{"shape", 1}}})
        for (const char* channel : {"components", "property_overrides"})
            for (const char* rows : {"entities", "members", "overrides"}) {
                Json entry{{channel, {{"forge.fog_volume", values}}}};
                Json document{{rows, Json::array({entry})}};
                if (std::string_view(rows) == "overrides")
                    document["base"] = AssetId::generate();
                check(document_runtime_features(document) == required,
                      "Scene/Prefab/variant Fog intent omitted the runtime requirement");
            }
    check(document_runtime_features({{"entities", Json::array({{{"components", Json::object()}}})}})
              .empty(),
          "Unrelated legacy document acquired a Fog requirement");
    AssetCatalog catalog(root / "fog-kit");
    AssetRecord document;
    document.id = AssetId::generate();
    document.type = "scene";
    document.source = "fog.scene.json";
    document.schema_version = 5;
    document.metadata["forge.runtime_document"] = {{"runtime_features", required}};
    catalog.add(document);
    validate_runtime_compatibility(catalog, {{"runtime_fog_volumes", fog_volume_profile_version}});
    reject([&] { validate_runtime_compatibility(catalog, Json::object()); },
           "Fog document accepted an older kit without local volume support");
    for (const Json& invalid : {Json(), Json(false), Json(0), Json(1.), Json("1"), Json(2)})
        reject(
            [&] { validate_runtime_compatibility(catalog, {{"runtime_fog_volumes", invalid}}); });
    for (const Json& invalid : {Json(), Json(false), Json(0), Json(1.), Json("1"), Json(2)}) {
        document.metadata["forge.runtime_document"]["runtime_features"]["fog_volumes"] = invalid;
        catalog.replace(document);
        reject([&] { validate_runtime_compatibility(catalog, {{"runtime_fog_volumes", 1}}); });
    }
}
} // namespace test_fog_package
