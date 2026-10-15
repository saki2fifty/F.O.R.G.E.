#include "../src/reconstructed_meta.hpp"
#include "../src/reflected_extensions.hpp"
#include "../src/runtime_property_tune.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/render_components.hpp>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void copied_metadata(EngineContext& engine, const Json& values) {
    using namespace forge::detail;
    auto world = engine.world().world();
    const ReflectedAdapter adapters[] = {
        {world.id<std::string>(), "string"},
        {world.id<std::vector<MaterialSlotOverride>>(), "vector"},
        {world.id<std::vector<MaterialParameterOverride>>(), "vector"},
        {world.id<AssetRef<MeshAsset>>(), "asset_ref", MeshAsset::type},
        {world.id<AssetRef<MaterialAsset>>(), "asset_ref", MaterialAsset::type},
        {world.id<AssetRef<MaterialVariantAsset>>(), "asset_ref", MaterialVariantAsset::type}};
    const auto full = reflected_type_schema(world, world.id<MeshRenderer>(), adapters);
    auto fields = Json::array();
    for (const auto& field : full.at("fields"))
        if (field.at("id") == "material_parameters")
            fields.push_back(field);
    require(fields.size() == 1 && fields[0].at("element_keys") == Json::array({"slot", "key"}),
            "Native collection did not project its composite business key");
    const Json projection{{"type", "struct"}, {"fields", fields}};
    flecs::world copied;
    const ReconstructedMeta owner(copied, projection);
    const Json source{{"material_parameters", values}};
    const ReflectedCandidate candidate(copied, owner.native_type(), source);
    const auto known = read_reflected_native(copied, owner.native_type(), candidate.data());
    require(merge_reflected_extensions(projection, known,
                                       reflected_extensions(projection, source)) == source,
            "Copied native metadata changed composite key/value/extension semantics");
    for (const auto keys : {Json::array({"slot", "slot"}), Json::array({"slot", "parameter"}),
                            Json::array({"slot"})}) {
        auto invalid = projection;
        invalid["fields"][0]["element_keys"] = keys;
        bool rejected = false;
        try {
            const ReconstructedMeta bad(copied, invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Malformed copied composite key metadata was accepted");
    }
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto id =
            authoring_command(scene, "entity.create", {{"name", "Object material values"}})
                .at("selected")
                .get<std::string>();
        authoring_command(scene, "component.add",
                          {{"entity", id}, {"component", "forge.mesh_renderer"}});
        auto legacy = scene.document();
        for (auto& row : legacy["entities"])
            if (row.at("id") == id)
                row["components"]["forge.mesh_renderer"].erase("material_parameters");
        scene.restore_snapshot(legacy);
        require(scene.entity(id).get<MeshRenderer>().material_parameters.empty(),
                "Legacy renderer did not receive empty object values");
        Json values =
            Json::array({{{"slot", "default"},
                          {"key", "roughnessFactor"},
                          {"parameter", {{"type", 0}, {"value", {.2f, 0.f, 0.f, 0.f}}}}}});
        auto color = values.front();
        color["key"] = "baseColorFactor";
        color["parameter"] = {{"type", unsigned(MaterialParameterType::LinearColor4)},
                              {"value", {.8f, .1f, .2f, 1.f}}};
        values.push_back(color);
        auto other = values.front();
        other["slot"] = "other";
        other["parameter"]["value"][0] = .9f;
        values.push_back(other);
        for (auto& row : values) {
            const auto owner =
                row.at("slot").get<std::string>() + "." + row.at("key").get<std::string>();
            row["future_row"] = {{"opaque", true}, {"owner", owner}};
            row["parameter"]["future_note"] = owner;
        }
        const auto before = scene.document();
        copied_metadata(engine, values);
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.mesh_renderer"},
                           {"field", "material_parameters"},
                           {"value", values}});
        const auto changed = scene.document();
        require(scene.entity(id).get<MeshRenderer>().material_parameters ==
                    std::vector<MaterialParameterOverride>{
                        {"default", "roughnessFactor", {MaterialParameterType::Scalar, {.2f}}},
                        {"default",
                         "baseColorFactor",
                         {MaterialParameterType::LinearColor4, {.8f, .1f, .2f, 1}}},
                        {"other", "roughnessFactor", {MaterialParameterType::Scalar, {.9f}}}},
                "Reflected array/struct admission disagrees with the native C++ values");
        require(scene.undo() && scene.document() == before && scene.redo() &&
                    scene.document() == changed,
                "Object values were not one atomic Scene Undo/Redo");
        EngineContext reopened;
        Scene copy(reopened.world());
        copy.restore_snapshot(changed);
        require(copy.document() == changed &&
                    copy.entity(id).get<MeshRenderer>() == scene.entity(id).get<MeshRenderer>(),
                "Object material values changed during Scene/native roundtrip");
        EngineContext runtime_engine(WorldRole::Runtime);
        Scene runtime(runtime_engine.world());
        runtime.restore_snapshot(changed);
        auto live_values = values;
        live_values[0]["parameter"]["value"][0] = .65f;
        const auto revision_before = runtime.revision();
        const auto live_result = runtime.tune_runtime_property(id, "forge.mesh_renderer",
                                                               "material_parameters", live_values);
        require(
            live_result[0].at("parameter").at("value")[0].get<float>() == .65f &&
                runtime.entity(id).get<MeshRenderer>().material_parameters[0].parameter.value[0] ==
                    .65f &&
                runtime.revision() > revision_before && !runtime.undo() && !runtime.redo() &&
                scene.document() == changed,
            "Live object values changed authoring state/history or failed native application");
        const auto live_native = runtime.entity(id).get<MeshRenderer>();
        for (const auto property : {"mesh", "materials", "enabled"}) {
            bool rejected = false;
            try {
                runtime.tune_runtime_property(id, "forge.mesh_renderer", property, nullptr);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && runtime.entity(id).get<MeshRenderer>() == live_native,
                    "Live material tuning admitted an unrelated builtin property");
        }
        auto oversized = live_values;
        oversized[0]["slot"] = std::string(256, 's');
        bool rejected_live = false;
        try {
            runtime.tune_runtime_property(id, "forge.mesh_renderer", "material_parameters",
                                          oversized);
        } catch (const std::exception&) {
            rejected_live = true;
        }
        require(rejected_live && runtime.entity(id).get<MeshRenderer>() == live_native,
                "Rejected live object values partially changed native state");
        runtime.restore_snapshot(changed);
        require(runtime.entity(id).get<MeshRenderer>() == scene.entity(id).get<MeshRenderer>(),
                "A fresh Play world retained transient material edits");
        auto native = scene.entity(id).get<MeshRenderer>();
        native.material_parameters.front().parameter.value[0] = .45f;
        std::reverse(native.material_parameters.begin(), native.material_parameters.end());
        scene.entity(id).set(native);
        const auto native_doc = scene.document();
        const auto& retained = native_doc.at("entities")[0]
                                   .at("components")
                                   .at("forge.mesh_renderer")
                                   .at("material_parameters");
        for (const auto& row : retained) {
            const auto owner =
                row.at("slot").get<std::string>() + "." + row.at("key").get<std::string>();
            require(row.at("future_row").at("owner") == owner &&
                        row.at("future_row").at("opaque") == true &&
                        row.at("parameter").at("future_note") == owner,
                    "Opaque extensions followed array position instead of the slot/key tuple");
        }
        authoring_command(scene, "entity.rename",
                          {{"entity", id}, {"name", "Renamed independently"}});
        require(scene.entity(id).get<MeshRenderer>() == native && scene.undo() &&
                    scene.document() == native_doc,
                "Unrelated authoring discarded native C++ material values");
        for (unsigned kind = 0; kind < 6; ++kind) {
            auto invalid = values;
            if (kind == 0)
                invalid.push_back(invalid.front());
            if (kind == 1)
                invalid[0]["parameter"]["type"] = .5;
            if (kind == 2)
                invalid[0]["parameter"]["value"] = {.2f, 0.f, 0.f};
            if (kind == 3)
                invalid[0]["parameter"]["value"][1] = 1;
            if (kind == 4)
                invalid[0]["slot"] = "physical/ordinal";
            if (kind == 5)
                invalid = Json::array();
            if (kind == 5)
                for (unsigned i = 0; i <= material_parameter_override_limit; ++i)
                    invalid.push_back(values.front());
            const auto held = scene.document();
            const auto revision = scene.revision();
            bool rejected = false;
            try {
                authoring_command(scene, "property.set",
                                  {{"entity", id},
                                   {"component", "forge.mesh_renderer"},
                                   {"field", "material_parameters"},
                                   {"value", invalid}});
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && scene.document() == held && scene.revision() == revision &&
                        scene.entity(id).get<MeshRenderer>() == native,
                    "Rejected object values changed native/authored data or revision");
        }
        std::cout << "Native material value layout, legacy empty default, Scene history/roundtrip, "
                     "C++ edits and rejected transaction retention passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
