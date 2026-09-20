#pragma once
#include <forge/audio_components.hpp>
#include <forge/navigation_components.hpp>
#include <forge/scene.hpp>
#include <limits>
#include <stdexcept>
inline void test_reflection() {
    using namespace forge;
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto schema = scene.schema();
    for (const auto& component : schema.at("components")) {
        const auto type = scene.world().lookup(component.at("id").get<std::string>().c_str());
        require(type.is_alive(), "Schema type does not exist in Flecs");
        require(component.at("description") == ecs_doc_get_brief(scene.world().c_ptr(), type),
                "Component help does not come from Doc");
        for (const auto& field : component.at("fields")) {
            auto member = type.lookup(field.at("id").get<std::string>().c_str());
            require(member && member.has<EcsMember>(), "Explicit reflected member missing");
            require(field.at("display_name") == ecs_doc_get_name(scene.world().c_ptr(), member) &&
                        field.at("description") == ecs_doc_get_brief(scene.world().c_ptr(), member),
                    "Field presentation does not match Doc");
            const auto& metadata = member.get<EcsMember>();
            if (metadata.unit) {
                const auto* unit = ecs_get(scene.world().c_ptr(), metadata.unit, EcsUnit);
                require(unit && field.at("unit") == unit->symbol, "Unit presentation mismatch");
            }
            if (field.contains("minimum")) {
                const auto& ranges = member.get<EcsMemberRanges>();
                require(field.at("minimum") == ranges.value.min &&
                            field.at("maximum") == ranges.value.max,
                        "Schema bounds differ from authoritative Meta ranges");
            }
        }
    }
    const auto translation = scene.world().component<LocalTranslation>().lookup("x");
    require(translation.get<EcsMember>().unit == EcsMeters,
            "Translation is not measured in meters");
    const auto rotation = scene.world().component<LocalRotation>().lookup("x");
    require(rotation.get<EcsMember>().unit == 0, "Quaternion incorrectly annotated as angle");
    const auto gain = scene.world().component<AudioSource>().lookup("gain");
    require(gain.get<EcsMemberRanges>().warning.max == 1 &&
                gain.get<EcsMemberRanges>().value.max == 4,
            "Recommended and allowed gain ranges conflated");
    Json audio;
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.audio_source")
            for (const auto& f : type.at("fields"))
                audio[f.at("id").get<std::string>()] = f.at("default");
    const auto id = EntityId::generate().str();
    Json doc = {{"version", 3},
                {"asset_id", AssetId::generate()},
                {"entities", Json::array({{{"id", id},
                                           {"name", "Sound"},
                                           {"components", {{"forge.audio_source", audio}}}}})}};
    doc["entities"][0]["components"]["forge.audio_source"]["gain"] = 2;
    scene.reset(doc); // Warning is advisory; supported amplification remains usable.
    // Existing builtin extension payloads keep their scene-envelope contract;
    // the new custom reflected-value budget must not truncate unknown data.
    doc["entities"][0]["components"]["forge.audio_source"]["future_extension"] =
        std::string(70000, 'x');
    scene.reset(doc);
    require(scene.document()
                    .at("entities")[0]
                    .at("components")
                    .at("forge.audio_source")
                    .at("future_extension")
                    .get<std::string>()
                    .size() == 70000,
            "Reflected admission discarded an existing opaque extension");
    const auto before = scene.document();
    auto reject = [&](Json invalid) {
        bool caught = false;
        try {
            scene.edit(invalid);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        require(caught && scene.document() == before && !scene.can_undo(),
                "Invalid metadata/domain value committed or polluted history");
    };
    auto invalid = doc;
    invalid["entities"][0]["components"]["forge.audio_source"]["gain"] = 4.1;
    reject(invalid);
    invalid = doc;
    invalid["entities"][0]["components"]["forge.audio_source"]["maximum_distance"] = .001;
    reject(invalid);
    invalid = doc;
    invalid["entities"][0]["components"]["forge.audio_source"]["pitch"] =
        std::numeric_limits<double>::infinity();
    reject(invalid);
    // Metadata does not veto a trusted native write. Admission checks, not Flecs
    // ranges, are what protect authored transactions and subsystem realization.
    scene.entity(id).set<AudioSource>(AudioSource{.gain = 5});
    require(scene.entity(id).get<AudioSource>().gain == 5, "Ranges unexpectedly treated as veto");
}
