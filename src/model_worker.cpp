#include "gltf_model_cook.hpp"
#include "model_importer.hpp"
namespace forge::asset_detail {
std::vector<ArtifactFile> execute_model_recipe(ImportProcessRequest request, std::stop_token stop) {
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    require(!stop.stop_requested(), "Model recipe cancelled");
    const auto& p = request.payload;
    require(p.at("recipe") == "forge.model.gltf" && p.at("revision") == model_recipe_revision(),
            "Model worker recipe/toolchain revision mismatch");
    const auto settings =
        model_settings().effective(p.at("settings").get<ImportSettingsDocument>());
    const auto backend = p.at("backend").get<std::string>();
    require(backend == "none" || backend == "d3d12", "Unsupported model worker backend");
    auto source = decode_gltf_snapshot(std::move(request.inputs), {}, stop);
    require(source.source_digest == p.at("source_digest").get<std::string>(),
            "Model source snapshot digest mismatch");
    for (const auto& required :
         source.document.value("extensionsRequired", nlohmann::json::array()))
        require(model_cook_extensions().contains(required.get<std::string>()),
                "Unsupported required extension in model recipe");
    GltfModelCookOptions options;
    auto directions = [](const nlohmann::json& value) {
        return value == "preserve"  ? MeshDirections::Preserve
               : value == "missing" ? MeshDirections::GenerateMissing
                                    : MeshDirections::Recalculate;
    };
    options.mesh.normals = directions(settings.at("normals"));
    options.mesh.tangents = directions(settings.at("tangents"));
    options.mesh.weld_exact = settings.at("weld_exact").get<bool>();
    options.mesh.optimize_vertex_fetch = settings.at("vertex_fetch").get<bool>();
    const auto compression = settings.at("compression");
    options.compression = compression == "bc" ? TextureCompression::NativeBc
                          : compression == "bc-high-quality"
                              ? TextureCompression::NativeBcHighQuality
                              : TextureCompression::None;
    options.maximum_texture_size = settings.at("max_texture_size").get<unsigned>();
    options.desktop_bc = backend == "d3d12";
    return cook_static_gltf_bundle(NativeGltfDocument(std::move(source)), options, stop);
}
} // namespace forge::asset_detail
