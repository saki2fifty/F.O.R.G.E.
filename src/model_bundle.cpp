#include "model_bundle.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "model_animation.hpp"
#include "model_scene_values.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <forge/material_asset.hpp>
#include <forge/mesh_asset.hpp>
#include <forge/texture_bundle.hpp>
#include <forge/transform.hpp>
#include <set>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t index_bytes = 16 * 1024 * 1024;
constexpr std::uint64_t total_bytes = 512ull * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::uint64_t number(const Json& j, std::uint64_t limit) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Invalid model bundle integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= limit, "Model bundle integer exceeds bounds");
    return n;
}
std::string text(const Json& j, std::size_t limit, bool empty = false) {
    require(j.is_string(), "Invalid model bundle text");
    const auto& s = j.get_ref<const std::string&>();
    require((empty || !s.empty()) && s.size() <= limit && s.find('\0') == std::string::npos,
            "Model bundle text exceeds bounds");
    return s;
}
void filename(std::string_view name) {
    require(!name.empty() && name.size() <= 128 && name.front() != '.' && name.back() != '.' &&
                name != "manifest.json" && name != "model.json",
            "Invalid model member filename");
    for (char c : name)
        require((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                    c == '.',
                "Nonportable model member filename");
    const auto stem = name.substr(0, name.find('.'));
    require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
                !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
                  stem[3] >= '1' && stem[3] <= '9'),
            "Reserved model member filename");
}
const Json& array(const Json& j, std::size_t limit) {
    require(j.is_array() && j.size() <= limit, "Model bundle array exceeds bounds");
    return j;
}
void node_index(const Json& j, std::size_t count, bool nullable = true) {
    if (nullable && j.is_null())
        return;
    require(number(j, count) < count, "Model node reference out of bounds");
}
void hierarchy(const Json& h, const std::map<std::string, const ModelImportMember*>& members) {
    const auto& nodes = array(h.at("nodes"), 100000);
    const auto cameras = h.value("cameras", Json::array());
    const auto lights = h.value("lights", Json::array());
    for (const auto& camera : array(cameras, 100000))
        require(model_camera_value(camera, true) == camera, "Noncanonical cooked camera value");
    for (const auto& light : array(lights, 100000))
        require(model_light_value(light, true) == light, "Noncanonical cooked light value");
    auto member = [&](const Json& value, std::string_view type) {
        if (value.is_null())
            return;
        const auto found = members.find(text(value, 4096));
        require(found != members.end() && found->second->identity.type == type,
                "Model hierarchy member reference has wrong type or is missing");
    };
    std::vector<std::vector<std::size_t>> children(nodes.size());
    std::vector<std::size_t> roots;
    std::size_t matrix_values = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        (void)text(node.at("name"), 4096, true);
        const auto& parent = node.at("parent");
        node_index(parent, nodes.size());
        if (parent.is_null())
            roots.push_back(i);
        else {
            const auto p = parent.get<std::size_t>();
            require(p != i, "Model node parents itself");
            children[p].push_back(i);
        }
        const auto& local = array(node.at("local"), 12);
        require(local.size() == 12, "Model node needs a row-major affine3x4");
        for (const auto& value : local)
            require(value.is_number() && std::isfinite(value.get<double>()),
                    "Nonfinite model transform");
        member(node.at("mesh"), "mesh");
        node_index(node.value("camera", Json(nullptr)), cameras.size());
        node_index(node.value("light", Json(nullptr)), lights.size());
        require(node.value("visible", Json(true)).is_boolean() &&
                    node.value("selectable", Json(true)).is_boolean(),
                "Invalid model visibility/selectability flag");
        const auto& weights = array(node.at("weights"), 256);
        matrix_values += weights.size();
        require(matrix_values <= 1000000, "Model morph defaults exceed bounds");
        for (const auto& value : weights) {
            require(value.is_number(), "Invalid model morph default");
            const auto x = value.get<double>();
            require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max() &&
                        (x == 0 || static_cast<float>(x) != 0),
                    "Invalid model morph default");
        }
    }
    std::size_t variant_mappings = 0;
    const auto material_variants = h.value("material_variants", Json::array());
    for (const auto& variant : array(material_variants, 65536)) {
        (void)text(variant.at("name"), 4096, true);
        std::set<std::pair<std::string, std::uint64_t>> mapped;
        for (const auto& mapping : array(variant.at("mappings"), 1000000)) {
            require(++variant_mappings <= 1000000, "Model variant mappings exceed bounds");
            require(!mapping.at("mesh").is_null() && !mapping.at("material").is_null(),
                    "Variant mapping requires mesh and material");
            member(mapping.at("mesh"), "mesh");
            member(mapping.at("material"), "material");
            const auto primitive = number(mapping.at("primitive"), 100000);
            require(mapped.emplace(mapping.at("mesh").get<std::string>(), primitive).second,
                    "Duplicate model variant primitive mapping");
        }
    }
    // Iterative traversal avoids recursion limits and quadratic ancestry walks.
    std::size_t visited = 0;
    auto pending = roots;
    std::vector<AffineTransform> world(nodes.size());
    while (!pending.empty()) {
        const auto i = pending.back();
        pending.pop_back();
        ++visited;
        world[i].m = nodes[i].at("local").get<std::array<double, 12>>();
        if (!nodes[i].at("parent").is_null())
            world[i] = world[nodes[i].at("parent").get<std::size_t>()] * world[i];
        for (auto x : world[i].m)
            require(std::isfinite(x), "Model composed transform is not finite");
        pending.insert(pending.end(), children[i].begin(), children[i].end());
    }
    require(visited == nodes.size(), "Model hierarchy contains a cycle");
    const auto& scenes = array(h.at("scenes"), 100000);
    node_index(h.at("default_scene"), scenes.size());
    std::size_t root_count = 0;
    for (const auto& scene : scenes) {
        std::set<std::size_t> unique;
        for (const auto& root : array(scene, nodes.size())) {
            node_index(root, nodes.size(), false);
            const auto i = root.get<std::size_t>();
            require(nodes[i].at("parent").is_null() && unique.insert(i).second,
                    "Model scene root is duplicated or has a parent");
            require(++root_count <= 1000000, "Model scene roots exceed bounds");
        }
    }
}
void validate(const ModelBundleIndex& index) {
    require(valid_content_digest(index.source_digest) && index.members.size() <= 4096 &&
                index.diagnostics.size() <= 4096,
            "Invalid model index source/counts");
    std::map<std::string, const ModelImportMember*> members;
    std::set<std::string> names;
    for (const auto& m : index.members) {
        const auto& id = m.identity;
        (void)text(id.address, 4096);
        (void)text(id.display_name, 4096, true);
        (void)text(id.evidence.exporter_key, 1024, true);
        require(id.type == "mesh" || id.type == "material" || id.type == "texture" ||
                    id.type == "skeleton" || id.type == "animation_clip",
                "Unsupported model bundle member type");
        for (const auto& digest : {id.evidence.content_digest, id.evidence.semantic_digest})
            require(digest.empty() || valid_content_digest(digest),
                    "Invalid model identity evidence");
        require(members.emplace(id.address, &m).second && names.insert(m.artifact.file).second,
                "Duplicate model member address/artifact");
        filename(m.artifact.file);
        require(valid_content_digest(m.artifact.digest) && m.artifact.bytes &&
                    m.artifact.bytes <= total_bytes && m.bindings.size() <= 65536,
                "Invalid model member artifact/bindings");
    }
    for (const auto& m : index.members) {
        for (const auto& [role, target] : m.bindings) {
            (void)text(role, 256);
            const auto found = members.find(target);
            require(found != members.end(), "Missing model binding target");
            require(
                (m.identity.type == "mesh" && found->second->identity.type == "material") ||
                    (m.identity.type == "material" && found->second->identity.type == "texture") ||
                    (m.identity.type == "animation_clip" && role == "skeleton" &&
                     found->second->identity.type == "skeleton"),
                "Invalid model binding type/direction");
        }
    }
    for (const auto& message : index.diagnostics)
        (void)text(message, 8192);
    hierarchy(index.hierarchy, members);
}
} // namespace
std::vector<std::byte> encode_model_bundle_index(const ModelBundleIndex& index) {
    validate(index);
    Json members = Json::array();
    for (const auto& m : index.members) {
        const auto& id = m.identity;
        members.push_back({{"address", id.address},
                           {"type", id.type},
                           {"name", id.display_name},
                           {"exporter_key", id.evidence.exporter_key},
                           {"content_digest", id.evidence.content_digest},
                           {"semantic_digest", id.evidence.semantic_digest},
                           {"file", m.artifact.file},
                           {"sha256", m.artifact.digest},
                           {"bytes", m.artifact.bytes},
                           {"bindings", m.bindings}});
    }
    const auto text =
        Json{{"format", "forge.model-bundle"},       {"version", 1},
             {"source_digest", index.source_digest}, {"members", members},
             {"hierarchy", index.hierarchy},         {"diagnostics", index.diagnostics}}
            .dump();
    require(text.size() <= index_bytes, "Model index exceeds byte limit");
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
ModelBundleIndex decode_model_bundle_index(std::span<const std::byte> bytes) {
    const auto j = parse_bounded_json(bytes, index_bytes);
    require(j.at("format") == "forge.model-bundle" && number(j.at("version"), 1) == 1,
            "Unsupported model bundle format");
    ModelBundleIndex result;
    result.source_digest = text(j.at("source_digest"), 64);
    for (const auto& m : array(j.at("members"), 4096)) {
        ModelImportMember member;
        member.identity = {text(m.at("address"), 4096),
                           text(m.at("type"), 256),
                           text(m.at("name"), 4096, true),
                           {text(m.at("exporter_key"), 1024, true),
                            text(m.at("content_digest"), 64, true),
                            text(m.at("semantic_digest"), 64, true)}};
        member.artifact = {text(m.at("file"), 128), text(m.at("sha256"), 64),
                           number(m.at("bytes"), total_bytes)};
        const auto& b = m.at("bindings");
        require(b.is_object() && b.size() <= 65536, "Invalid model member bindings");
        for (const auto& [key, target] : b.items())
            member.bindings.emplace(key, text(target, 4096));
        result.members.push_back(std::move(member));
    }
    result.hierarchy = j.at("hierarchy");
    for (const auto& d : array(j.at("diagnostics"), 4096))
        result.diagnostics.push_back(text(d, 8192));
    validate(result);
    return result;
}
ModelBundleIndex validate_model_bundle(std::span<const ArtifactFile> files, ModelValidation stage) {
    require(files.size() <= 8192, "Model output file count exceeds bounds");
    std::map<std::string, const ArtifactFile*> lookup;
    std::uint64_t size = 0;
    for (const auto& file : files) {
        require(file.bytes.size() <= total_bytes - size && lookup.emplace(file.name, &file).second,
                "Model artifact size/duplicate name");
        size += file.bytes.size();
    }
    auto take = [&](const std::string& name, const std::string& digest,
                    std::uint64_t bytes) -> const ArtifactFile& {
        const auto found = lookup.find(name);
        require(found != lookup.end(), "Model artifact file missing");
        const auto* result = found->second;
        require(result->bytes.size() == bytes && content_digest(result->bytes) == digest,
                "Model artifact digest/size mismatch");
        lookup.erase(found);
        return *result;
    };
    const auto model = lookup.find("model.json");
    require(model != lookup.end(), "Model index missing");
    auto index = decode_model_bundle_index(model->second->bytes);
    lookup.erase(model);
    const bool pending = index.hierarchy.value("animation_pending", false);
    require(!pending ||
                (stage == ModelValidation::GeometryStage && !index.hierarchy.contains("animation")),
            "Incomplete model animation stage cannot be published");
    std::vector<ArtifactFile> archives;
    std::map<std::string, const ModelImportMember*> animation_members;
    std::map<std::string, std::vector<std::uint32_t>> mesh_palettes;
    std::map<std::string, std::map<TextureSemantic, TextureDimension>> variants;
    std::map<std::string, MaterialData> materials;
    std::map<std::string, std::size_t> morph_counts;
    std::map<std::string, std::vector<std::set<std::string>>> primitive_streams;
    std::map<std::string, std::set<std::string>> material_targets;
    std::map<std::string, std::vector<std::string>> primitive_materials;
    for (const auto& m : index.members) {
        const auto& file = take(m.artifact.file, m.artifact.digest, m.artifact.bytes);
        if (m.identity.type == "mesh") {
            const auto mesh = decode_mesh(file.bytes);
            auto& palette = mesh_palettes[m.identity.address];
            for (const auto& lod : mesh.lods)
                for (const auto& part : lod.parts) {
                    // A skin-bound node must have prepared influence streams on every draw.
                    palette.push_back(part.joint_palette.empty()
                                          ? UINT32_MAX
                                          : *std::max_element(part.joint_palette.begin(),
                                                              part.joint_palette.end()));
                }
            morph_counts[m.identity.address] = mesh.morph_defaults.size();
            auto& streams = primitive_streams[m.identity.address];
            for (const auto& part : mesh.lods.at(0).parts) {
                std::set<std::string> names;
                for (const auto& stream : part.streams)
                    names.insert(stream.semantic);
                streams.push_back(std::move(names));
                if (part.material_slot) {
                    const auto slot =
                        m.bindings.find("material." + std::to_string(part.material_slot));
                    require(slot != m.bindings.end(), "Model primitive material is not bound");
                    primitive_materials[m.identity.address].push_back(slot->second);
                } else
                    primitive_materials[m.identity.address].emplace_back();
            }
            for (const auto& [role, target] : m.bindings) {
                (void)role;
                material_targets[m.identity.address].insert(target);
            }
            for (const auto& lod : mesh.lods)
                for (const auto& part : lod.parts)
                    require(
                        part.material_slot == 0 ||
                            m.bindings.contains("material." + std::to_string(part.material_slot)),
                        "Model mesh has an unbound nondefault material slot");
            for (const auto& [role, target] : m.bindings) {
                (void)target;
                require(role.starts_with("material."), "Unknown mesh material binding role");
                const auto text = std::string_view(role).substr(9);
                std::uint32_t slot = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), slot);
                require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                            role == "material." + std::to_string(slot) &&
                            slot < mesh.material_slots,
                        "Mesh material binding slot out of range");
            }
        } else if (m.identity.type == "material") {
            auto material = decode_material(file.bytes);
            require(material.textures.size() == m.bindings.size(),
                    "Model material binding count mismatch");
            for (const auto& [role, target] : m.bindings) {
                (void)target;
                require(material.textures.contains(role), "Unknown model material texture slot");
            }
            materials.emplace(m.identity.address, std::move(material));
        } else if (m.identity.type == "skeleton" || m.identity.type == "animation_clip") {
            archives.push_back(file);
            animation_members.emplace(m.identity.address, &m);
        } else {
            const auto bundle = decode_texture_bundle_index(file.bytes);
            for (const auto& entry : bundle.variants) {
                const auto& variant = take(entry.file, entry.digest, entry.bytes);
                const auto texture = decode_texture(variant.bytes);
                require(texture.semantic == entry.semantic, "Model texture semantic mismatch");
                variants[m.identity.address].emplace(entry.semantic, texture.dimension);
            }
        }
    }
    for (const auto& m : index.members)
        if (m.identity.type == "material")
            for (const auto& [role, target] : m.bindings) {
                const auto& slot = materials.at(m.identity.address).textures.at(role);
                const auto& textures = variants.at(target);
                const auto selected = textures.find(slot.semantic);
                require(selected != textures.end() && selected->second == slot.dimension,
                        "Model texture lacks compatible semantic/dimension variant");
            }
    for (const auto& node : index.hierarchy.at("nodes"))
        if (!node.at("weights").empty())
            require(!node.at("mesh").is_null() &&
                        morph_counts.at(node.at("mesh").get<std::string>()) ==
                            node.at("weights").size(),
                    "Model node morph defaults disagree with mesh");
    for (const auto& [mesh, parts] : primitive_materials)
        for (std::size_t part = 0; part < parts.size(); ++part)
            if (!parts[part].empty())
                for (const auto& [role, slot] : materials.at(parts[part]).textures) {
                    (void)role;
                    require(primitive_streams.at(mesh).at(part).contains(
                                "TEXCOORD_" + std::to_string(slot.uv_set)),
                            "Model material needs unavailable texture coordinates");
                }
    for (const auto& variant : index.hierarchy.value("material_variants", Json::array()))
        for (const auto& mapping : variant.at("mappings")) {
            const auto mesh = mapping.at("mesh").get<std::string>();
            const auto material = mapping.at("material").get<std::string>();
            const auto part = mapping.at("primitive").get<std::size_t>();
            require(part < primitive_streams.at(mesh).size() &&
                        material_targets[mesh].contains(material),
                    "Model variant references absent primitive or unbound material");
            for (const auto& [role, slot] : materials.at(material).textures) {
                (void)role;
                require(primitive_streams.at(mesh).at(part).contains("TEXCOORD_" +
                                                                     std::to_string(slot.uv_set)),
                        "Model variant needs unavailable texture coordinates");
            }
        }
    if (index.hierarchy.contains("animation")) {
        const auto& animation = index.hierarchy.at("animation");
        const auto& meta = animation.at("plan");
        const auto& provenance = animation.at("provenance");
        require(provenance.at("converter") == "gltf2ozz" &&
                    provenance.at("converter_revision") ==
                        "744eb9d99f606eda849acb0b1204f7a3dc20bca1" &&
                    valid_content_digest(provenance.at("converter_sha256").get<std::string>()) &&
                    provenance.at("source_digest") == index.source_digest,
                "Model animation provenance is invalid");
        validate_model_animation(meta, archives);
        const auto skeleton_address = animation.at("skeleton").get<std::string>();
        const auto skeleton = animation_members.find(skeleton_address);
        require(skeleton != animation_members.end() &&
                    skeleton->second->identity.type == "skeleton" &&
                    skeleton->second->artifact.file == "skeleton.ozz" &&
                    skeleton->second->bindings.empty(),
                "Missing model skeleton binding");
        const auto& clip_addresses = animation.at("clips");
        require(clip_addresses.is_array() && clip_addresses.size() == meta.at("clips").size() &&
                    animation_members.size() == clip_addresses.size() + 1,
                "Model animation member count mismatch");
        std::set<std::string> bound;
        for (std::size_t i = 0; i < clip_addresses.size(); ++i) {
            const auto address = clip_addresses[i].get<std::string>();
            const auto found = animation_members.find(address);
            require(found != animation_members.end() && bound.insert(address).second &&
                        found->second->identity.type == "animation_clip" &&
                        found->second->artifact.file == meta.at("clips")[i].at("file") &&
                        found->second->bindings ==
                            std::map<std::string, std::string>{{"skeleton", skeleton_address}},
                    "Model clip skeleton binding mismatch");
        }
        const auto& nodes = index.hierarchy.at("nodes");
        require(meta.at("node_skins").size() == nodes.size(), "Model skin node count mismatch");
        std::map<std::size_t, std::size_t> joints;
        for (std::size_t i = 0; i < meta.at("joint_nodes").size(); ++i)
            joints.emplace(meta.at("joint_nodes")[i].get<std::size_t>(), i);
        std::vector<AffineTransform> world(joints.size());
        for (std::size_t i = 0; i < world.size(); ++i) {
            const auto node_index = meta.at("joint_nodes")[i].get<std::size_t>();
            const auto& node = nodes.at(node_index);
            const auto parent = meta.at("joint_parents")[i].get<int>();
            require(node.at("parent").is_null()
                        ? parent == -1
                        : (joints.contains(node.at("parent").get<std::size_t>()) && parent >= 0 &&
                           joints.at(node.at("parent").get<std::size_t>()) == std::size_t(parent)),
                    "Rig ancestry differs from model geometry");
            world[i].m = node.at("local").get<std::array<double, 12>>();
            if (parent >= 0)
                world[i] = world[std::size_t(parent)] * world[i];
            for (unsigned col = 0; col < 4; ++col) {
                double magnitude = 1;
                for (unsigned row = 0; row < 3; ++row)
                    magnitude = std::max(magnitude, std::abs(world[i].m[row * 4 + col]));
                for (unsigned row = 0; row < 3; ++row)
                    require(
                        std::abs(world[i].m[row * 4 + col] -
                                 meta.at("joint_rest_models")[i][col * 4 + row].get<double>()) <=
                            magnitude * 5e-5,
                        "Rig rest pose differs from model geometry");
            }
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& skin_index = meta.at("node_skins")[i];
            if (skin_index.is_null())
                continue;
            require(!nodes[i].at("mesh").is_null(), "Skin bound to model node without mesh");
            const auto count =
                meta.at("skins").at(skin_index.get<std::size_t>()).at("joints").size();
            for (auto joint : mesh_palettes.at(nodes[i].at("mesh").get<std::string>()))
                require(joint < count,
                        "Model skin requires prepared palette within skin joint bounds");
        }
        for (const auto& clip : meta.at("clips"))
            for (const auto& track : clip.at("morph_tracks")) {
                const auto& node = nodes.at(track.at("node").get<std::size_t>());
                require(!node.at("mesh").is_null() &&
                            morph_counts.at(node.at("mesh").get<std::string>()) ==
                                track.at("components").get<std::size_t>(),
                        "Model morph animation target count differs from mesh");
            }
    } else
        require(archives.empty(), "Animation archives without complete model binding");
    require(lookup.empty(), "Unexpected file in model artifact");
    return index;
}
std::vector<ArtifactFile> complete_model_animation(std::vector<ArtifactFile> geometry,
                                                   const Json& metadata,
                                                   std::vector<ArtifactFile> archives,
                                                   const Json& provenance) {
    auto index = validate_model_bundle(geometry, ModelValidation::GeometryStage);
    require(index.hierarchy.value("animation_pending", false),
            "Model animation completion requires an incomplete geometry candidate");
    validate_model_animation(metadata, archives);
    const std::string skeleton_address = "/rig/skeleton";
    Json clips = Json::array();
    auto add = [&](std::string address, std::string type, std::string name, const Json& evidence,
                   const std::string& filename) {
        const auto found = std::find_if(archives.begin(), archives.end(),
                                        [&](const auto& f) { return f.name == filename; });
        require(found != archives.end(), "Missing admitted model animation archive");
        ModelImportMember member;
        member.identity = {std::move(address),
                           std::move(type),
                           std::move(name),
                           {"", evidence.at("content_evidence").get<std::string>(),
                            evidence.at("semantic_evidence").get<std::string>()}};
        member.artifact = {filename, content_digest(found->bytes), found->bytes.size()};
        if (member.identity.type == "animation_clip")
            member.bindings.emplace("skeleton", skeleton_address);
        index.members.push_back(std::move(member));
    };
    add(skeleton_address, "skeleton", "Model skeleton", metadata, "skeleton.ozz");
    for (std::size_t i = 0; i < metadata.at("clips").size(); ++i) {
        const auto& clip = metadata.at("clips")[i];
        const auto address = "/animations/" + std::to_string(i);
        clips.push_back(address);
        add(address, "animation_clip", clip.at("name").get<std::string>(), clip,
            clip.at("file").get<std::string>());
    }
    index.hierarchy.erase("animation_pending");
    index.hierarchy["animation"] = {{"plan", metadata},
                                    {"provenance", provenance},
                                    {"skeleton", skeleton_address},
                                    {"clips", clips}};
    for (auto& file : geometry)
        if (file.name == "model.json")
            file.bytes = encode_model_bundle_index(index);
    for (auto& file : archives)
        geometry.push_back(std::move(file));
    (void)validate_model_bundle(geometry);
    return geometry;
}
} // namespace forge::asset_detail
