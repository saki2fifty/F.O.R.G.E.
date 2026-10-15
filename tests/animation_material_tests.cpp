#include "animation_material_values.hpp"
#include "engine_render_resource.hpp"
#include "transform_animation_runtime.hpp"
#include <cmath>
#include <iostream>
#include <limits>
using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid animated material operation accepted");
}
std::vector<AnimationMaterialValues> sample(const TransformAnimationSource& source, double time) {
    const auto clip = source.clip();
    std::vector<AnimationMaterialValues> values(clip.material_bindings.size());
    clip.sample_materials(time, values);
    return values;
}
void source_and_sampling() {
    auto source = TransformAnimationSource::create(AssetId::generate());
    const auto first = source.clip().keys.front().id;
    const std::array names{"scalar", "vector2", "vector3", "vector4", "color3", "color4"};
    const std::array<AnimationMaterialValues, 6> ends{
        {{1}, {1, 2}, {1, 2, 3}, {1, 2, 3, 4}, {.2, .4, .6}, {1, .5, .25, 1}}};
    for (unsigned kind = 0; kind < names.size(); ++kind)
        source.add_material_parameter({"default", names[kind], MaterialParameterType(kind)}, {});
    source.document["channels"] = Json::array();
    source.document["future"] = {{"unchanged", 7}};
    source.document["keys"][0]["material_values"][0]["future"] = "retained";
    const auto last = source.set_key(2 * animation_timebase, {});
    for (unsigned kind = 0; kind < names.size(); ++kind)
        source.set_material_value(last, "default", names[kind], ends[kind]);
    check(source.document.at("version") == 5 && source.clip().channels == 0 &&
              source.clip().material_bindings.size() == names.size(),
          "Material-only clip lost its explicit schema/bindings");
    const auto middle = sample(source, animation_timebase);
    for (unsigned kind = 0; kind < names.size(); ++kind) {
        check(source.clip().material_bindings[kind].type == MaterialParameterType(kind),
              "Material vector/color semantics were aliased");
        for (unsigned lane = 0; lane < 4; ++lane)
            check(std::abs(middle[kind][lane] - ends[kind][lane] / 2) < 1e-12,
                  "Linear material sample differs from independent interpolation");
    }
    auto reordered = source;
    std::reverse(reordered.document["material_bindings"].begin(),
                 reordered.document["material_bindings"].end());
    for (auto& key : reordered.document["keys"])
        std::reverse(key["material_values"].begin(), key["material_values"].end());
    const auto reversed = sample(reordered, animation_timebase);
    for (unsigned i = 0; i < names.size(); ++i)
        check(reversed[i] == middle[names.size() - 1 - i],
              "Material binding used a source row ordinal as identity");
    auto held = source;
    held.document["keys"][0]["interpolation"] = "held";
    check(sample(held, animation_timebase)[0][0] == 0, "Held material key interpolated");
    source.set_material_curve(first, "default", "scalar", 0, {true, 0, 2});
    check(sample(source, animation_timebase)[0][0] == 1,
          "Material cubic curve ignored its endpoint velocity");
    auto retimed = source;
    retimed.retime_duration(4 * animation_timebase);
    check(sample(retimed, 2 * animation_timebase) == sample(source, animation_timebase),
          "Whole-clip retiming changed the material curve shape");
    auto edited = source;
    edited.add_property(AnimationProperty::LightIntensity, 1);
    edited.set_property_curve(first, AnimationProperty::LightIntensity, {true, 0, 0});
    check(edited.document.at("version") == 5 &&
              sample(edited, animation_timebase) == sample(source, animation_timebase),
          "Scalar curve editing downgraded material schema or values");
    const auto duplicated = edited.duplicate_keys(std::array{first}, animation_timebase / 2);
    check(duplicated.size() == 1 && duplicated[0] != first &&
              edited.document.at("keys")[1].at("material_values")[0].at("future") == "retained" &&
              edited.document.at("future") == source.document.at("future"),
          "Key duplication lost material values, identities or extensions");
    const auto cooked = source.cook();
    const auto decoded = decode_transform_animation(cooked, source.asset());
    std::vector<AnimationMaterialValues> loaded(decoded.material_bindings.size());
    decoded.sample_materials(animation_timebase, loaded);
    check(loaded == sample(source, animation_timebase) &&
              decoded.material_bindings == source.clip().material_bindings,
          "Cooked material clip differs from source sampling");
    auto wrong_envelope =
        Json::parse(std::string_view(reinterpret_cast<const char*>(cooked.data()), cooked.size()));
    wrong_envelope["version"] = 4;
    const auto text = wrong_envelope.dump();
    reject([&] { decode_transform_animation(std::as_bytes(std::span(text)), source.asset()); });
    const auto before = source.document;
    reject([&] { source.set_material_value(last, "default", "scalar", {1, 1}); });
    reject([&] { source.set_material_curve(first, "default", "scalar", 1, {}); });
    reject([&] {
        source.add_material_parameter({"default", "scalar", MaterialParameterType::Vector2}, {});
    });
    check(source.document == before, "Rejected material edit changed source");
    auto invalid = source;
    invalid.document["keys"][0]["material_values"][1]["key"] = "scalar";
    reject([&] { invalid.validate(); });
    invalid = source;
    invalid.document["keys"][0]["material_values"][1]["value"].push_back(0);
    reject([&] { invalid.validate(); });
    auto large = source;
    large.set_material_value(first, "default", "scalar", {9e8});
    large.set_material_value(last, "default", "scalar", {9e8});
    const auto original = large.document;
    reject([&] { large.set_material_curve(first, "default", "scalar", 0, {true, 0, 1e9}); });
    check(large.document == original, "Overshooting material curve changed source");
    std::array<AnimationMaterialValues, 1> short_buffer{};
    reject([&] { decoded.sample_materials(0, short_buffer); });
}
void legacy_and_limits() {
    auto property_extension = TransformAnimationSource::create(AssetId::generate());
    property_extension.add_property(AnimationProperty::LightIntensity, 2);
    const auto property_first = property_extension.clip().keys.front().id;
    property_extension.set_key(0, {}, false, property_first, TransformAnimationCurves{});
    auto properties = property_extension.clip().keys.front().properties;
    properties[unsigned(AnimationProperty::LightIntensity)] = 6;
    property_extension.set_key(2 * animation_timebase, {}, false, {}, {}, properties);
    property_extension.document["keys"][0]["curves"]["property_curves"] = {
        {"forge.light.intensity", {{"interpolation", "cubic"}, {"incoming", 0}, {"outgoing", 0}}}};
    check(property_extension.document.at("version") == 3 &&
              property_extension.clip().sample_properties(
                  animation_timebase / 2)[unsigned(AnimationProperty::LightIntensity)] == 3,
          "A source3 extension gained scalar curve meaning before promotion");
    const auto property_before = property_extension.document;
    reject([&] {
        property_extension.add_material_parameter(
            {"default", "roughnessFactor", MaterialParameterType::Scalar}, {.5});
    });
    check(property_extension.document == property_before,
          "Material promotion activated a legacy valid-looking scalar curve extension");
    auto legacy = TransformAnimationSource::create(AssetId::generate());
    legacy.document["material_bindings"] = {{"opaque", true}};
    legacy.document["keys"][0]["material_values"] = {{"opaque", true}};
    legacy.validate();
    check(legacy.clip().material_bindings.empty() && legacy.clip().keys[0].materials.empty(),
          "Legacy opaque material fields gained meaning or dense storage");
    const auto before = legacy.document;
    reject([&] {
        legacy.add_material_parameter({"default", "test", MaterialParameterType::Scalar}, {});
    });
    check(legacy.document == before, "Legacy material promotion collision rewrote extensions");
    const auto wrap =
        Json{{"format", "forge.transform-clip"}, {"version", 4}, {"document", legacy.document}}
            .dump();
    check(decode_transform_animation(std::as_bytes(std::span(wrap)), legacy.asset())
              .material_bindings.empty(),
          "Old selected cooked4 clip was reinterpreted");
    auto dense = TransformAnimationSource::create(AssetId::generate());
    dense.document["version"] = 5;
    dense.document["channels"] = Json::array();
    dense.document["material_bindings"] = Json::array();
    auto key = dense.document["keys"][0];
    key["material_values"] = Json::array();
    for (unsigned i = 0; i < 64; ++i) {
        const auto name = "p" + std::to_string(i);
        dense.document["material_bindings"].push_back(
            {{"slot", "default"},
             {"key", name},
             {"type", unsigned(MaterialParameterType::Vector4)}});
        key["material_values"].push_back(
            {{"slot", "default"}, {"key", name}, {"value", {0, 0, 0, 0}}});
    }
    dense.document["keys"] = Json::array();
    for (unsigned i = 0; i < 64; ++i) {
        key["id"] = AssetId::generate();
        key["time_us"] = i * 1000;
        dense.document["keys"].push_back(key);
    }
    dense.validate();
    auto clip = dense.clip();
    ResourcePool<TransformAnimationAsset> pool({1, 2, 2, 256 * 1024});
    const auto ticket = pool.request({dense.asset()}, std::string(64, 'a'), 1,
                                     [clip = std::move(clip)](std::stop_token) {
                                         const auto memory = clip.resident_bytes();
                                         return ResourceCandidate<TransformAnimationAsset>{
                                             std::make_unique<TransformAnimationClip>(clip),
                                             ResourceMemory{.animation = memory}};
                                     });
    check(!pool.wait(ticket, std::chrono::seconds(5)) &&
              ticket.inspect().state == ResourceState::Failed &&
              ticket.inspect().diagnostic.find("budget") != std::string::npos,
          "Dynamic material key storage bypassed actual resource memory budget");
    dense.document["material_bindings"].push_back(
        {{"slot", "default"}, {"key", "p64"}, {"type", 0}});
    reject([&] { dense.validate(); });
}
void curve_authoring() {
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.add_material_parameter({"surface", "factor", MaterialParameterType::Scalar}, {});
    const auto first = source.clip().keys.front().id;
    const auto middle = source.set_key(animation_timebase, {});
    const auto last = source.set_key(2 * animation_timebase, {});
    source.set_material_value(middle, "surface", "factor", {1});
    source.set_material_curve(middle, "surface", "factor", 0, {true, 0, 0});
    source.document["keys"][1]["material_values"][0]["curves"][0]["future"] = 73;
    source.set_material_curves(std::array{first, middle}, "surface", "factor", 0, {true, .25, -.5});
    auto clip = source.clip();
    check(clip.keys[0].materials[0].curves[0].outgoing == -.5 &&
              clip.keys[1].materials[0].curves[0].incoming == .25 &&
              !clip.keys[2].materials[0].curves[0].cubic,
          "Bulk material curve edit changed an unselected key");
    const std::array keys{first, middle, last};
    source.set_material_tangent_preset(keys, "surface", "factor", 0,
                                       TransformAnimationTangentPreset::Linear);
    check(sample(source, animation_timebase / 4)[0][0] == .25 &&
              sample(source, 5 * animation_timebase / 4)[0][0] == .75,
          "Material linear preset does not reproduce segment secants");
    source.set_material_tangent_preset(keys, "surface", "factor", 0,
                                       TransformAnimationTangentPreset::Smooth);
    check(sample(source, animation_timebase / 2)[0][0] == .625 &&
              source.clip().keys[1].materials[0].curves[0].incoming == 0 &&
              source.clip().keys[1].materials[0].curves[0].outgoing == 0,
          "Material smooth preset lost its independent monotonic-turn oracle");
    source.set_material_tangent_preset(keys, "surface", "factor", 0,
                                       TransformAnimationTangentPreset::Flat);
    check(sample(source, animation_timebase / 4)[0][0] == .15625 &&
              source.document["keys"][1]["material_values"][0]["curves"][0]["future"] == 73 &&
              source.document.at("version") == 5,
          "Material flat preset changed curve shape, source semantics or extensions");
    const AnimationMaterialCurveEdit edit{
        {"surface", "factor", MaterialParameterType::Scalar}, 0, {true, .125, .25}};
    source.set_material_curves(middle, std::array{edit});
    check(source.clip().keys[1].materials[0].curves[0].incoming == .125 &&
              source.document["keys"][1]["material_values"][0]["curves"][0]["future"] == 73,
          "Stable-tuple curve transaction changed extensions or applied a different lane");
    const auto before = source.document;
    reject(
        [&] { source.set_material_curves(middle, std::span<const AnimationMaterialCurveEdit>{}); });
    reject([&] { source.set_material_curves(middle, std::array{edit, edit}); });
    auto retyped = edit;
    retyped.binding.type = MaterialParameterType::Vector3;
    reject([&] { source.set_material_curves(middle, std::array{edit, retyped}); });
    reject([&] { source.set_material_curves(AssetId::generate(), std::array{edit}); });
    reject([&] { source.set_material_curves({}, "surface", "factor", 0, {}); });
    reject(
        [&] { source.set_material_curves(std::array{first, first}, "surface", "factor", 0, {}); });
    reject([&] {
        source.set_material_curves(std::array{AssetId::generate()}, "surface", "factor", 0, {});
    });
    reject([&] { source.set_material_curves(keys, "surface", "factor", 1, {}); });
    reject([&] {
        source.set_material_curves(keys, "surface", "factor", 0,
                                   {true, std::numeric_limits<double>::infinity(), 0});
    });
    reject([&] {
        source.set_material_tangent_preset(keys, "surface", "factor", 0,
                                           TransformAnimationTangentPreset(99));
    });
    check(source.document == before, "Rejected bulk material curve edit changed source");
}
template <class T>
ResourceLease<T> load(ResourcePool<T>& pool, typename ResourceTraits<T>::Data source) {
    const auto ticket =
        pool.request(AssetRef<T>{AssetId::generate()}, std::string(64, 'b'), 1,
                     [source = std::move(source)](std::stop_token) mutable {
                         auto value =
                             std::make_unique<typename ResourceTraits<T>::Data>(std::move(source));
                         const auto bytes = value->resident_bytes();
                         return ResourceCandidate<T>{std::move(value), {bytes}};
                     });
    check(pool.wait(ticket, std::chrono::seconds(5)), "Material target test resource failed");
    return pool.acquire(ticket);
}
void target_admission() {
    using namespace asset_detail;
    ResourcePool<MeshAsset> meshes;
    ResourcePool<MaterialAsset> materials;
    auto mesh = engine_mesh_resource(engine_primitive(0));
    mesh.mesh.lods[0].parts.push_back(mesh.mesh.lods[0].parts.front());
    const auto slot = mesh.materials.front().key;
    MaterialResourceData material;
    material.values.model = "forge.gltf.metallic-roughness.v1";
    material.values.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.2f}};
    const auto a = load(materials, material);
    material.values.parameters["roughnessFactor"].value[0] = .8f;
    const auto b = load(materials, material);
    PreparedModelDraw draw;
    draw.mesh = load(meshes, std::move(mesh));
    draw.selection = select_mesh_materials(draw.mesh.get(), {});
    draw.selection.parts[0] = {{a.identity().asset}, {b.identity().asset}};
    draw.materials = {{a.identity().asset, a}, {b.identity().asset, b}};
    const auto identity = prepared_model_draw_key(draw, false);
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.add_material_parameter({slot, "roughnessFactor", MaterialParameterType::Scalar}, {.2});
    source.document["channels"] = Json::array();
    const auto first = source.clip().keys.front().id;
    const auto last = source.set_key(animation_timebase, {});
    source.set_material_value(last, slot, "roughnessFactor", {.8});
    const std::vector current{
        MaterialParameterOverride{slot, "normalScale", {MaterialParameterType::Scalar, {-0.f}}}};
    const auto before = source.document;
    auto clip = source.clip();
    validate_animation_material_target(clip, draw, current);
    const auto sampled =
        sample_animation_material_parameters(clip, animation_timebase / 2, current);
    check(sampled.size() == 2 && sampled[0] == current[0] &&
              std::signbit(sampled[0].parameter.value[0]) &&
              sampled[1].parameter == MaterialParameter{MaterialParameterType::Scalar, {.5f}},
          "Material sampling lost sparse intent, unrelated values or native precision");
    check(source.document == before && prepared_model_draw_key(draw, false) == identity &&
              a->values.parameters.at("roughnessFactor").value[0] == .2f &&
              b->values.parameters.at("roughnessFactor").value[0] == .8f,
          "Animation target validation changed shared material/source/resource identity");
    auto cubic = source;
    cubic.set_material_value(first, slot, "roughnessFactor", {.5});
    cubic.set_material_value(last, slot, "roughnessFactor", {.5});
    cubic.set_material_curve(first, slot, "roughnessFactor", 0, {true, 0, 4});
    cubic.set_material_curve(last, slot, "roughnessFactor", 0, {true, -4, 0});
    clip = cubic.clip();
    check(clip.material_range(0, 0) == std::array<double, 2>{.5, 1.5},
          "Whole-clip bounds missed the independent interior cubic extremum");
    reject([&] { validate_animation_material_target(clip, draw, current); });
    cubic.document["keys"][0]["interpolation"] = "held";
    clip = cubic.clip();
    check(clip.material_range(0, 0) == std::array<double, 2>{.5, .5},
          "Held material segment admitted inactive cubic slopes");
    validate_animation_material_target(clip, draw, current);
    reject([&] { clip.material_range(1, 0); });
    reject([&] { clip.material_range(0, 1); });
    auto missing = source;
    missing.document["material_bindings"][0]["slot"] = "missing";
    for (auto& key : missing.document["keys"])
        key["material_values"][0]["slot"] = "missing";
    reject([&] { validate_animation_material_target(missing.clip(), draw, current); });
    const std::vector wrong{MaterialParameterOverride{
        slot, "roughnessFactor", {MaterialParameterType::Vector2, {.2f, .4f}}}};
    reject([&] { sample_animation_material_parameters(source.clip(), 0, wrong); });
    check(wrong[0].parameter.type == MaterialParameterType::Vector2 &&
              current[0].parameter.value[0] == 0 && std::signbit(current[0].parameter.value[0]),
          "Rejected animated candidate changed its caller's values");
    auto unavailable = draw;
    unavailable.material_fallbacks[b.identity().asset] = a.identity().asset;
    reject([&] { validate_animation_material_target(source.clip(), unavailable, current); });
    std::vector<MaterialParameterOverride> full;
    for (unsigned i = 0; i < 64; ++i)
        full.push_back({slot, "p" + std::to_string(i), {}});
    reject([&] { sample_animation_material_parameters(source.clip(), 0, full); });
    // Each part must admit the exact semantic type, even at identical numeric widths.
    SurfaceShaderDefinition declaration;
    declaration.parameters["color"] = {MaterialParameterType::LinearColor3, {.1f, .2f, .3f}};
    MaterialResourceData surface;
    surface.values = surface_material_defaults(declaration);
    surface.surface.emplace();
    surface.surface->program.surface = declaration;
    const auto custom = load(materials, std::move(surface));
    auto color = TransformAnimationSource::create(AssetId::generate());
    color.add_material_parameter({slot, "color", MaterialParameterType::LinearColor3},
                                 {.25, .5, .75});
    draw.materials.emplace(custom.identity().asset, custom);
    draw.selection.parts[0] = {{custom.identity().asset}, {custom.identity().asset}};
    validate_animation_material_target(color.clip(), draw, {});
    auto vector = color;
    vector.document["material_bindings"][0]["type"] = unsigned(MaterialParameterType::Vector3);
    reject([&] { validate_animation_material_target(vector.clip(), draw, {}); });
    draw.selection.parts[0][1] = {a.identity().asset};
    reject([&] { validate_animation_material_target(color.clip(), draw, {}); });
    // Valid numeric keys must not switch the selected transmission pass later in the clip.
    material.values.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.5f}};
    const auto optical = load(materials, material);
    draw.materials.emplace(optical.identity().asset, optical);
    draw.selection.parts[0] = {{optical.identity().asset}, {optical.identity().asset}};
    auto metal = TransformAnimationSource::create(AssetId::generate());
    metal.add_material_parameter({slot, "metallicFactor", MaterialParameterType::Scalar}, {1});
    const auto end = metal.set_key(animation_timebase, {});
    metal.set_material_value(end, slot, "metallicFactor", {.4});
    reject([&] { validate_animation_material_target(metal.clip(), draw, {}); });
}
} // namespace
int main() {
    try {
        source_and_sampling();
        legacy_and_limits();
        curve_authoring();
        target_admission();
        std::cout
            << "Material animation source, sampling, identity, legacy and budget checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
