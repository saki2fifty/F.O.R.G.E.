#include "engine_render_resource.hpp"
#include "material_parameter_overrides.hpp"
#include "model_draw_candidate.hpp"
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F run) {
    try {
        run();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid object material selection accepted");
}
template <class T>
ResourceLease<T> load(ResourcePool<T>& pool, typename ResourceTraits<T>::Data source) {
    const auto ticket =
        pool.request(AssetRef<T>{AssetId::generate()}, std::string(64, 'a'), 1,
                     [source = std::move(source)](std::stop_token) mutable {
                         auto data =
                             std::make_unique<typename ResourceTraits<T>::Data>(std::move(source));
                         const auto bytes = data->resident_bytes();
                         return ResourceCandidate<T>{std::move(data), {bytes}};
                     });
    require(pool.wait(ticket, std::chrono::seconds(5)), "Test resource failed to load");
    return pool.acquire(ticket);
}
} // namespace
int main() {
    try {
        ResourcePool<MeshAsset> meshes;
        ResourcePool<MaterialAsset> materials;
        auto mesh = engine_mesh_resource(engine_primitive(0));
        mesh.mesh.lods[0].parts.push_back(mesh.mesh.lods[0].parts.front());
        const auto slot = mesh.materials.front().key;
        MaterialResourceData base;
        base.values.model = "forge.gltf.metallic-roughness.v1";
        base.values.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.2f}};
        const auto a = load(materials, base);
        base.values.parameters["roughnessFactor"].value[0] = .8f;
        const auto b = load(materials, base);
        PreparedModelDraw prepared;
        prepared.mesh = load(meshes, std::move(mesh));
        prepared.selection = select_mesh_materials(prepared.mesh.get(), {});
        prepared.selection.bindings[0].material = {a.identity().asset};
        prepared.selection.parts[0] = {{a.identity().asset}, {b.identity().asset}};
        prepared.materials = {{a.identity().asset, a}, {b.identity().asset, b}};
        const auto identity = prepared_model_draw_key(prepared, false);
        const std::vector values{MaterialParameterOverride{
            slot, "roughnessFactor", {MaterialParameterType::Scalar, {.2f}}}};
        const auto changed = prepare_model_material_parameters(prepared, values);
        require(changed.size() == 1 && changed[0] == values[0] &&
                    prepared_model_draw_key(prepared, false) == identity,
                "Part-specific variant defaults erased an override or changed resource identity");
        prepared.selection.parts[0][1] = {a.identity().asset};
        require(prepare_model_material_parameters(prepared, values).empty() && values.size() == 1,
                "Equal defaults were not normalized or changed authored intent");
        auto surface = base;
        SurfaceShaderDefinition declaration;
        declaration.parameters["roughnessFactor"] = {MaterialParameterType::Vector2, {.2f, .3f}};
        surface.values = surface_material_defaults(declaration);
        surface.surface.emplace();
        surface.surface->program.surface = declaration;
        const auto c = load(materials, surface);
        prepared.materials.emplace(c.identity().asset, c);
        prepared.selection.parts[0][1] = {c.identity().asset};
        rejects([&] { prepare_model_material_parameters(prepared, values); });
        auto unknown = values;
        unknown[0].slot = "removed-slot";
        rejects([&] { prepare_model_material_parameters(prepared, unknown); });
        prepared.selection.parts[0][1] = {a.identity().asset};
        auto invalid = values;
        invalid[0].parameter.value[0] = 2;
        rejects([&] { prepare_model_material_parameters(prepared, invalid); });
        const auto missing = AssetId::generate();
        prepared.selection.parts[0] = {{missing}, {missing}};
        prepared.material_fallbacks[missing] = engine_material(EngineMaterial::Error).id;
        require(prepare_model_material_parameters(prepared, values).empty(),
                "Missing-material fallback consumed an unavailable declaration");
        prepared.material_fallbacks.clear();
        rejects([&] { prepare_model_material_parameters(prepared, values); });
        std::cout << "Complete part-specific material admission, normalization, failure isolation "
                     "and unchanged resource identity passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
