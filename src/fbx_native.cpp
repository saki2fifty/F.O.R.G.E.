#include "fbx_native.hpp"
#include "asset_bytes.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <ufbx.h>

namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t source_limit = 512ull * 1024 * 1024;
constexpr std::size_t interchange_limit = 256ull * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::string text(ufbx_string value) {
    require(value.length <= 4096 && (value.data || !value.length), "FBX text exceeds bounds");
    std::string result(value.data ? value.data : "", value.length);
    require(result.find('\0') == std::string::npos, "FBX text contains a null character");
    return result;
}
std::string uid(const ufbx_element& element) {
    const auto* dom = element.dom_node;
    require(dom && dom->values.count && dom->values.data[0].type == UFBX_DOM_VALUE_NUMBER,
            "FBX element requires an original numeric source UID (FBX7 or later)");
    const auto value = dom->values.data[0].value_int;
    require(value != 0, "FBX element has reserved zero source UID");
    return std::to_string(value);
}
std::string node_key(const ufbx_node& node) {
    if (node.is_root)
        return "fbx:root";
    if (node.is_geometry_transform_helper || node.is_scale_helper) {
        require(node.parent && !node.parent->is_geometry_transform_helper &&
                    !node.parent->is_scale_helper,
                "Unsupported nested FBX transform helper");
        return "fbx:node:" + uid(node.parent->element) +
               (node.is_geometry_transform_helper ? ":geometry" : ":scale");
    }
    return "fbx:node:" + uid(node.element);
}
ufbx_progress_result progress(void* user, const ufbx_progress*) {
    return static_cast<std::stop_token*>(user)->stop_requested() ? UFBX_PROGRESS_CANCEL
                                                                 : UFBX_PROGRESS_CONTINUE;
}
using Scene = std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)>;
Scene load(std::span<const std::byte> source, std::stop_token& stop) {
    require(!stop.stop_requested(), "FBX parsing cancelled");
    require(!source.empty() && source.size() <= source_limit, "FBX source exceeds bounds");
    ufbx_load_opts options{};
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.retain_dom = true;
    options.load_external_files = false;
    options.open_file_cb.fn = [](void*, ufbx_stream*, const char*, size_t,
                                 const ufbx_open_file_info*) { return false; };
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.node_depth_limit = 256;
    options.index_error_handling = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    options.unicode_error_handling = UFBX_UNICODE_ERROR_HANDLING_ABORT_LOADING;
    options.temp_allocator.memory_limit = source_limit;
    options.result_allocator.memory_limit = source_limit;
    options.progress_cb.fn = progress;
    options.progress_cb.user = &stop;
    options.progress_interval_hint = 1024 * 1024;
    ufbx_error error{};
    Scene scene(ufbx_load_memory(source.data(), source.size(), &options, &error), ufbx_free_scene);
    if (!scene) {
        char message[2048];
        ufbx_format_error(message, sizeof(message), &error);
        throw std::runtime_error(std::string("FBX parser: ") + message);
    }
    require(scene->metadata.file_format == UFBX_FILE_FORMAT_FBX &&
                scene->metadata.version >= 7000 && scene->metadata.version <= 7700 &&
                scene->nodes.count <= 100000 &&
                scene->meshes.count + scene->materials.count + scene->textures.count <= 4096 &&
                scene->anim_stacks.count <= 64,
            "FBX version or scene counts exceed the supported profile");
    return scene;
}
std::string image_path(const ufbx_texture& texture) {
    auto path = text(texture.relative_filename);
    if (path.empty())
        path = text(texture.filename);
    std::replace(path.begin(), path.end(), '\\', '/');
    require(!path.empty() && path.front() != '/' && path.find(':') == std::string::npos,
            "FBX image requires a project-contained relative filename");
    return path;
}
Json description(const ufbx_scene& scene) {
    Json images = Json::array();
    for (const auto* texture : scene.textures) {
        if (texture->type != UFBX_TEXTURE_FILE)
            continue;
        if (!texture->content.size)
            images.push_back(
                {{"key", "fbx:image:" + uid(texture->element)}, {"path", image_path(*texture)}});
    }
    return {{"version", 1},
            {"parser_revision", fbx_parser_revision},
            {"animation", scene.anim_stacks.count != 0 || scene.skin_deformers.count != 0},
            {"images", std::move(images)}};
}
Json trs(const ufbx_transform& t) {
    const auto& p = t.translation;
    const auto& q = t.rotation;
    const auto& s = t.scale;
    for (double value : {p.x, p.y, p.z, q.x, q.y, q.z, q.w, s.x, s.y, s.z})
        require(std::isfinite(value), "FBX transform is not finite");
    return {{"translation", {p.x, p.y, p.z}},
            {"rotation", {q.x, q.y, q.z, q.w}},
            {"scale", {s.x, s.y, s.z}}};
}
double material_alpha(const ufbx_material& material) {
    if (material.pbr.opacity.has_value)
        return material.pbr.opacity.value_real;
    const auto& transparency = material.fbx.transparency_color;
    const auto& factor = material.fbx.transparency_factor;
    const auto color = transparency.has_value ? transparency.value_vec3 : ufbx_vec3{};
    const auto amount = factor.has_value ? factor.value_real : 1.;
    for (double value : {color.x, color.y, color.z, amount})
        require(std::isfinite(value) && value >= 0 && value <= 1,
                "FBX transparency must contain finite values in [0,1]");
    require(std::abs(color.x - color.y) <= 1e-6 && std::abs(color.x - color.z) <= 1e-6,
            "FBX colored transparency is unsupported; use neutral transparency or an authored "
            "material");
    return 1 - amount * (color.x + color.y + color.z) / 3;
}
struct InterchangeBuilder {
    FbxInterchange result;
    std::vector<std::byte> bytes;
    std::stop_token stop;
    Json& doc() { return result.generated.document; }
    void check() const { require(!stop.stop_requested(), "FBX conversion cancelled"); }
    void identity(std::string address, std::string key) {
        require(result.evidence.emplace(std::move(address), SubassetEvidence{key, "", ""}).second,
                "Duplicate FBX interchange member");
    }
    std::size_t accessor(const std::vector<float>& values, unsigned width, bool bounds = false,
                         bool joints = false) {
        check();
        require(width && !values.empty() && values.size() % width == 0 &&
                    values.size() <= (interchange_limit - bytes.size()) / 4,
                "FBX expanded geometry exceeds interchange budget");
        while (bytes.size() % 4)
            bytes.push_back(std::byte{});
        const auto offset = bytes.size();
        for (float value : values) {
            require(std::isfinite(value), "FBX geometry exceeds finite float storage");
            require(!joints || (value >= 0 && value <= 65535 && std::floor(value) == value),
                    "Invalid FBX joint index");
            const auto bits =
                joints ? static_cast<std::uint32_t>(value) : std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < (joints ? 2u : 4u); ++i)
                bytes.push_back(std::byte((bits >> (i * 8)) & 255));
        }
        const auto view = doc()["bufferViews"].size(), index = doc()["accessors"].size();
        doc()["bufferViews"].push_back(
            {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bytes.size() - offset}});
        doc()["accessors"].push_back({{"bufferView", view},
                                      {"componentType", joints ? 5123 : 5126},
                                      {"count", values.size() / width},
                                      {"type", width == 1    ? "SCALAR"
                                               : width == 2  ? "VEC2"
                                               : width == 3  ? "VEC3"
                                               : width == 16 ? "MAT4"
                                                             : "VEC4"}});
        if (bounds) {
            std::vector<float> minimum(values.begin(), values.begin() + width), maximum = minimum;
            for (std::size_t i = 0; i < values.size(); ++i) {
                minimum[i % width] = std::min(minimum[i % width], values[i]);
                maximum[i % width] = std::max(maximum[i % width], values[i]);
            }
            doc()["accessors"][index]["min"] = minimum;
            doc()["accessors"][index]["max"] = maximum;
        }
        return index;
    }
};
} // namespace
nlohmann::json inspect_fbx(std::span<const std::byte> source, std::stop_token stop) {
    return description(*load(source, stop));
}
FbxInterchange convert_fbx_interchange(
    std::span<const std::byte> source,
    const std::map<std::string, std::shared_ptr<const std::vector<std::byte>>>& images,
    const Json& settings, std::stop_token stop) {
    auto scene = load(source, stop);

    InterchangeBuilder b{{}, {}, stop};
    require(scene->metadata.warnings.count <= 4096, "FBX parser warning count exceeds bounds");
    for (const auto& warning : scene->metadata.warnings) {
        require(warning.type != UFBX_WARNING_UNSUPPORTED_VERSION,
                "FBX parser reports an unsupported source version");
        b.result.generated.diagnostics.push_back("FBX parser notice: " + text(warning.description));
    }
    b.doc() = {{"asset", {{"version", "2.0"}, {"generator", "FORGE FBX worker interchange"}}},
               {"nodes", Json::array()},
               {"meshes", Json::array()},
               {"materials", Json::array()},
               {"images", Json::array()},
               {"textures", Json::array()},
               {"samplers", Json::array()},
               {"bufferViews", Json::array()},
               {"accessors", Json::array()}};
    std::map<const ufbx_node*, std::size_t> nodes;
    std::set<std::string> node_keys;
    for (const auto* node : scene->nodes) {
        b.check();
        const auto key = node_key(*node);
        require(node_keys.insert(key).second, "FBX contains duplicate source node UIDs");
        const auto index = b.doc()["nodes"].size();
        nodes.emplace(node, index);
        auto value = trs(node->local_transform);
        value["name"] = node->is_root ? "FBX root" : text(node->name);
        b.doc()["nodes"].push_back(std::move(value));
        b.identity("/nodes/" + std::to_string(index), key);
    }
    for (const auto* node : scene->nodes) {
        auto& value = b.doc()["nodes"][nodes.at(node)];
        for (const auto* child : node->children)
            value["children"].push_back(nodes.at(child));
        if (!node->visible) {
            value["extensions"]["KHR_node_visibility"]["visible"] = false;
            b.doc()["extensionsUsed"] = {"KHR_node_visibility"};
        }
        if (node->camera || node->light || (node->attrib && !node->mesh && !node->bone))
            b.result.generated.diagnostics.push_back(
                "FBX attachment retained as a transform only: " + text(node->name));
    }
    b.doc()["scenes"] = Json::array({{{"nodes", {nodes.at(scene->root_node)}}}});
    b.doc()["scene"] = 0;
    std::map<const ufbx_texture*, std::size_t> textures;
    const auto texture_binding = [&](const ufbx_material_map& map,
                                     const ufbx_material* material) -> Json {
        if (!map.texture || !map.texture_enabled)
            return nullptr;
        const auto* texture = map.texture;
        require(texture->type == UFBX_TEXTURE_FILE && !texture->has_uv_transform,
                "FBX material requires a file texture without a UV transform");
        std::optional<std::size_t> uv;
        const auto uv_name = text(texture->uv_set);
        if (!uv_name.empty()) {
            for (const auto* node : scene->nodes) {
                if (!node->mesh || std::find(node->materials.begin(), node->materials.end(),
                                             material) == node->materials.end())
                    continue;
                std::optional<std::size_t> matched;
                for (std::size_t i = 0; i < node->mesh->uv_sets.count; ++i)
                    if (text(node->mesh->uv_sets.data[i].name) == uv_name)
                        matched = i;
                require(matched && (!uv || *uv == *matched),
                        "FBX named texture UV set is missing or has inconsistent indices across "
                        "material uses");
                uv = matched;
            }
        }
        if (const auto at = textures.find(texture); at != textures.end())
            return {{"index", at->second}, {"texCoord", uv.value_or(0)}};
        const auto key = "fbx:image:" + uid(texture->element);
        std::shared_ptr<const std::vector<std::byte>> encoded;
        if (texture->content.size) {
            require(texture->content.size <= source_limit, "FBX embedded image exceeds limits");
            const auto* first = static_cast<const std::byte*>(texture->content.data);
            encoded = std::make_shared<const std::vector<std::byte>>(first,
                                                                     first + texture->content.size);
        } else {
            const auto found = images.find(key);
            require(found != images.end(), "FBX image is missing from the captured snapshot");
            encoded = found->second;
        }
        const auto image = b.doc()["images"].size(), index = b.doc()["textures"].size();
        b.doc()["images"].push_back({{"name", text(texture->name)}});
        b.result.generated.images.push_back({{encoded, 0, encoded->size()}, ""});
        b.identity("/images/" + std::to_string(image), key);
        b.doc()["samplers"].push_back(
            {{"wrapS", texture->wrap_u == UFBX_WRAP_REPEAT ? 10497 : 33071},
             {"wrapT", texture->wrap_v == UFBX_WRAP_REPEAT ? 10497 : 33071}});
        b.doc()["textures"].push_back({{"source", image}, {"sampler", index}});
        textures.emplace(texture, index);
        return {{"index", index}, {"texCoord", uv.value_or(0)}};
    };
    std::map<const ufbx_material*, std::size_t> materials;
    for (const auto* material : scene->materials) {
        b.check();
        const auto index = b.doc()["materials"].size();
        materials.emplace(material, index);
        const auto& p = material->pbr;
        const auto c = p.base_color.has_value ? p.base_color.value_vec3 : ufbx_vec3{1, 1, 1};
        const auto factor = p.base_factor.has_value ? p.base_factor.value_real : 1.;
        const auto alpha = material_alpha(*material);
        Json value{{"name", text(material->name)},
                   {"pbrMetallicRoughness",
                    {{"baseColorFactor", {c.x * factor, c.y * factor, c.z * factor, alpha}},
                     {"metallicFactor", p.metalness.has_value ? p.metalness.value_real : 0.},
                     {"roughnessFactor", p.roughness.has_value ? p.roughness.value_real : 1.}}}};
        if (alpha < 1)
            value["alphaMode"] = "BLEND";
        if (material->features.double_sided.enabled)
            value["doubleSided"] = true;
        if (auto binding = texture_binding(p.base_color, material); !binding.is_null())
            value["pbrMetallicRoughness"]["baseColorTexture"] = std::move(binding);
        if (auto binding = texture_binding(p.normal_map, material); !binding.is_null())
            value["normalTexture"] = std::move(binding);
        if (auto binding = texture_binding(p.emission_color, material); !binding.is_null())
            value["emissiveTexture"] = std::move(binding);
        const auto e = p.emission_color.has_value ? p.emission_color.value_vec3 : ufbx_vec3{};
        const auto emission = p.emission_factor.has_value ? p.emission_factor.value_real : 1.;
        value["emissiveFactor"] = {e.x * emission, e.y * emission, e.z * emission};
        for (const auto& binding : material->textures)
            if (binding.texture && binding.texture != p.base_color.texture &&
                binding.texture != p.normal_map.texture &&
                binding.texture != p.emission_color.texture)
                b.result.generated.diagnostics.push_back(
                    "FBX material texture is outside the supported base-color/normal/emission "
                    "profile: " +
                    text(binding.material_prop));
        b.doc()["materials"].push_back(std::move(value));
        b.identity("/materials/" + std::to_string(index), "fbx:material:" + uid(material->element));
    }
    b.doc()["skins"] = Json::array();
    std::map<const ufbx_skin_deformer*, std::size_t> skins;
    for (const auto* skin : scene->skin_deformers) {
        b.check();
        require(
            skin->clusters.count && skin->clusters.count <= 1024 &&
                (skin->skinning_method == UFBX_SKINNING_METHOD_LINEAR ||
                 skin->skinning_method == UFBX_SKINNING_METHOD_RIGID) &&
                skin->max_weights_per_vertex <= 64,
            "FBX skin requires linear weights, at most 1024 joints and 64 influences per vertex");
        Json joints = Json::array();
        std::vector<float> matrices;
        std::set<const ufbx_node*> distinct;
        for (const auto* cluster : skin->clusters) {
            require(cluster->bone_node && distinct.insert(cluster->bone_node).second,
                    "FBX skin cluster is missing its bone or repeats a bone");
            joints.push_back(nodes.at(cluster->bone_node));
            const auto& m = cluster->geometry_to_bone;
            for (unsigned c = 0; c < 4; ++c) {
                for (unsigned r = 0; r < 3; ++r)
                    matrices.push_back(float(m.cols[c].v[r]));
                matrices.push_back(c == 3 ? 1.f : 0.f);
            }
        }
        const auto index = b.doc()["skins"].size();
        skins.emplace(skin, index);
        b.doc()["skins"].push_back(
            {{"joints", joints}, {"inverseBindMatrices", b.accessor(matrices, 16)}});
    }
    std::map<const ufbx_mesh*, std::pair<std::size_t, const ufbx_node*>> meshes;
    std::size_t total_corners = 0;
    for (const auto* node : scene->nodes) {
        b.check();
        if (!node->mesh)
            continue;
        const auto* mesh = node->mesh;
        require(mesh->skin_deformers.count <= 1,
                "FBX mesh with multiple skin deformers is unsupported");
        const auto* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        if (skin) {
            require(skin->vertices.count == mesh->num_vertices,
                    "FBX skin vertex count differs from geometry");
            b.doc()["nodes"][nodes.at(node)]["skin"] = skins.at(skin);
        }
        const auto weight_sets = skin ? (skin->max_weights_per_vertex + 3) / 4 : 0;
        if (const auto at = meshes.find(mesh); at != meshes.end()) {
            const auto& other = at->second.second->materials;
            require(other.count == node->materials.count &&
                        std::equal(other.begin(), other.end(), node->materials.begin()),
                    "FBX mesh instances with different material assignments are not supported");
            b.doc()["nodes"][nodes.at(node)]["mesh"] = at->second.first;
            continue;
        }
        require(!mesh->blend_deformers.count && !mesh->cache_deformers.count &&
                    !mesh->num_line_faces && !mesh->num_point_faces && mesh->num_triangles &&
                    mesh->num_triangles <= (8 * 1024 * 1024 - total_corners) / 3 &&
                    mesh->uv_sets.count <= 4,
                "FBX geometry is empty, unsupported or exceeds expanded vertex limits");
        const auto expanded_width = 3 + (mesh->vertex_normal.exists ? 3 : 0) +
                                    (mesh->vertex_normal.exists && mesh->vertex_tangent.exists &&
                                             mesh->vertex_bitangent.exists
                                         ? 4
                                         : 0) +
                                    mesh->uv_sets.count * 2 + (mesh->vertex_color.exists ? 4 : 0) +
                                    weight_sets * 8;
        require(mesh->num_triangles * 3 <=
                    (interchange_limit - b.bytes.size()) / 4 / expanded_width,
                "FBX expanded attribute streams exceed the interchange budget");
        total_corners += mesh->num_triangles * 3;
        const auto index = b.doc()["meshes"].size();
        meshes.emplace(mesh, std::pair{index, node});
        b.doc()["nodes"][nodes.at(node)]["mesh"] = index;
        Json primitives = Json::array();
        std::map<std::size_t, std::vector<std::uint32_t>> parts;
        std::vector<std::uint32_t> triangle;
        for (std::size_t f = 0; f < mesh->faces.count; ++f) {
            b.check();
            const auto face = mesh->faces.data[f];
            require(face.num_indices >= 3 && face.num_indices <= 65536 &&
                        (!mesh->face_hole.count || !mesh->face_hole.data[f]),
                    "FBX polygon is empty, oversized or has unsupported holes");
            triangle.resize((face.num_indices - 2) * 3);
            const auto count = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh, face);
            require(count == face.num_indices - 2, "FBX polygon could not be fully triangulated");
            const auto material = mesh->face_material.count ? mesh->face_material.data[f] : 0;
            auto& corners = parts[material];
            corners.insert(corners.end(), triangle.begin(), triangle.end());
        }
        for (const auto& [material, corners] : parts) {
            std::map<std::string, std::vector<float>> streams;
            for (const auto corner : corners) {
                const auto p = ufbx_get_vertex_vec3(&mesh->vertex_position, corner);
                streams["POSITION"].insert(streams["POSITION"].end(),
                                           {float(p.x), float(p.y), float(p.z)});
                if (mesh->vertex_normal.exists) {
                    const auto n = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
                    streams["NORMAL"].insert(streams["NORMAL"].end(),
                                             {float(n.x), float(n.y), float(n.z)});
                }
                if (mesh->vertex_normal.exists && mesh->vertex_tangent.exists &&
                    mesh->vertex_bitangent.exists) {
                    const auto n = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
                    const auto t = ufbx_get_vertex_vec3(&mesh->vertex_tangent, corner);
                    const auto bitangent = ufbx_get_vertex_vec3(&mesh->vertex_bitangent, corner);
                    const auto sign = (n.y * t.z - n.z * t.y) * bitangent.x +
                                      (n.z * t.x - n.x * t.z) * bitangent.y +
                                      (n.x * t.y - n.y * t.x) * bitangent.z;
                    auto& values = streams["TANGENT"];
                    values.insert(values.end(),
                                  {float(t.x), float(t.y), float(t.z), sign < 0 ? 1.f : -1.f});
                }
                for (std::size_t uv = 0; uv < mesh->uv_sets.count; ++uv) {
                    const auto p = ufbx_get_vertex_vec2(&mesh->uv_sets.data[uv].vertex_uv, corner);
                    auto& values = streams["TEXCOORD_" + std::to_string(uv)];
                    values.insert(values.end(), {float(p.x), float(1 - p.y)});
                }
                if (skin) {
                    const auto control = mesh->vertex_indices.data[corner];
                    const auto& vertex = skin->vertices.data[control];
                    require(
                        vertex.num_weights && vertex.num_weights <= weight_sets * 4,
                        "FBX skin vertex is unweighted or exceeds its declared influence count");
                    for (std::size_t set = 0; set < weight_sets; ++set) {
                        auto& joints = streams["JOINTS_" + std::to_string(set)];
                        auto& weights = streams["WEIGHTS_" + std::to_string(set)];
                        for (unsigned k = 0; k < 4; ++k) {
                            const auto at = set * 4 + k;
                            if (at < vertex.num_weights) {
                                const auto& weight = skin->weights.data[vertex.weight_begin + at];
                                joints.push_back(float(weight.cluster_index));
                                weights.push_back(float(weight.weight));
                            } else {
                                joints.push_back(0);
                                weights.push_back(0);
                            }
                        }
                    }
                }
                if (mesh->vertex_color.exists) {
                    const auto c = ufbx_get_vertex_vec4(&mesh->vertex_color, corner);
                    auto& values = streams["COLOR_0"];
                    values.insert(values.end(), {float(c.x), float(c.y), float(c.z), float(c.w)});
                }
            }
            Json attributes = Json::object();
            for (const auto& [semantic, values] : streams)
                attributes[semantic] = b.accessor(
                    values,
                    semantic.starts_with("TEXCOORD") ? 2
                    : semantic == "COLOR_0" || semantic == "TANGENT" ||
                            semantic.starts_with("JOINTS_") || semantic.starts_with("WEIGHTS_")
                        ? 4
                        : 3,
                    semantic == "POSITION", semantic.starts_with("JOINTS_"));
            Json primitive{{"attributes", attributes}, {"mode", 4}};
            if (node->materials.count) {
                require(material < node->materials.count, "FBX face material is out of range");
                primitive["material"] = materials.at(node->materials.data[material]);
            }
            primitives.push_back(std::move(primitive));
        }
        b.doc()["meshes"].push_back({{"name", text(mesh->name)}, {"primitives", primitives}});
        b.identity("/meshes/" + std::to_string(index), "fbx:mesh:" + uid(mesh->element));
    }
    b.doc()["animations"] = Json::array();
    std::size_t animation_values = 0;
    for (const auto* stack : scene->anim_stacks) {
        b.check();
        const auto begin = stack->time_begin, end = stack->time_end;
        require(std::isfinite(begin) && std::isfinite(end) && end > begin && end - begin <= 3600,
                "FBX clip needs a positive duration of at most one hour");
        ufbx_bake_opts options{};
        options.temp_allocator.memory_limit = 128 * 1024 * 1024;
        options.result_allocator.memory_limit = 128 * 1024 * 1024;
        options.resample_rate = settings.value("animation_sampling_rate", 30u);
        options.minimum_sample_rate = options.resample_rate;
        options.maximum_sample_rate = 240;
        options.key_reduction_enabled = false;
        ufbx_error error{};
        std::unique_ptr<ufbx_baked_anim, decltype(&ufbx_free_baked_anim)> baked(
            ufbx_bake_anim(scene.get(), stack->anim, &options, &error), ufbx_free_baked_anim);
        require(bool(baked), "FBX clip could not be baked within the animation budget");
        for (const auto& element : baked->elements)
            for (const auto& property : element.props) {
                const auto name = text(property.name);
                if (name == "Lcl Translation" || name == "Lcl Rotation" || name == "Lcl Scaling")
                    continue;
                require(b.result.generated.diagnostics.size() < 4096,
                        "FBX animation notices exceed bounds");
                b.result.generated.diagnostics.push_back(
                    "FBX animated property is not evaluated: " + name);
            }
        Json animation{
            {"name", text(stack->name)}, {"samplers", Json::array()}, {"channels", Json::array()}};
        for (const auto& node : baked->nodes) {
            b.check();
            require(node.typed_id < scene->nodes.count, "FBX baked node is out of range");
            const auto original = nodes.at(scene->nodes.data[node.typed_id]);
            const auto channel = [&](const auto& keys, const char* path, unsigned width) {
                if (!keys.count)
                    return;
                require(keys.count + 2 <= (4 * 1024 * 1024 - animation_values) / (width + 1),
                        "FBX baked animation exceeds 16 MiB");
                animation_values += (keys.count + 2) * (width + 1);
                std::vector<float> times, values;
                float previous = -1;
                const auto append = [&](double time, const auto& value) {
                    const auto t = float(time);
                    require(t > previous, "FBX animation key times collapse in float storage");
                    previous = t;
                    times.push_back(t);
                    for (unsigned c = 0; c < width; ++c)
                        values.push_back(float(value.v[c]));
                };
                const auto evaluate = [&](double time) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(keys.data[0].value)>,
                                                 ufbx_quat>)
                        return ufbx_evaluate_baked_quat(keys, time);
                    else
                        return ufbx_evaluate_baked_vec3(keys, time);
                };
                append(0, evaluate(begin));
                for (const auto& key : keys) {
                    require(std::isfinite(key.time), "Nonfinite FBX baked animation time");
                    if (key.time > begin && key.time < end)
                        append(key.time - begin, key.value);
                }
                append(end - begin, evaluate(end));
                const auto input = b.accessor(times, 1, true), output = b.accessor(values, width);
                const auto sampler = animation["samplers"].size();
                animation["samplers"].push_back(
                    {{"input", input}, {"output", output}, {"interpolation", "LINEAR"}});
                animation["channels"].push_back(
                    {{"sampler", sampler}, {"target", {{"node", original}, {"path", path}}}});
            };
            channel(node.translation_keys, "translation", 3);
            channel(node.rotation_keys, "rotation", 4);
            channel(node.scale_keys, "scale", 3);
        }
        require(!animation["channels"].empty(), "FBX clip has no supported transform animation");
        const auto index = b.doc()["animations"].size();
        b.doc()["animations"].push_back(std::move(animation));
        b.identity("/animations/" + std::to_string(index), "fbx:clip:" + uid(stack->element));
        b.result.generated.diagnostics.push_back(
            "FBX clip curves are baked to linear/quaternion keys at the selected sample rate; "
            "constant steps use bounded transition keys.");
    }
    if (scene->skin_deformers.count || scene->anim_stacks.count)
        b.identity("/rig/skeleton", "fbx:combined-rig");
    b.doc()["buffers"] = Json::array({{{"byteLength", b.bytes.size()}}});
    auto storage = std::make_shared<const std::vector<std::byte>>(std::move(b.bytes));
    b.result.generated.buffers.push_back({storage, 0, storage->size()});
    // This digest authenticates the generated interchange, never the FBX source.
    b.result.generated.source_digest =
        asset_build_digest({{"document", b.doc()}, {"buffer", content_digest(*storage)}});
    b.result.generated.source = "fbx-interchange.gltf";
    return std::move(b.result);
}
} // namespace forge::asset_detail
