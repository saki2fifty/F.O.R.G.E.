#include <algorithm>
#include <cmath>
#include <cstring>
#include <forge/surface_shader.hpp>
#include <set>
namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void identifier(std::string_view name) {
    require(!name.empty() && name.size() <= 128, "Surface identifier exceeds bounds");
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                    (i && c >= '0' && c <= '9'),
                "Invalid surface identifier");
    }
}
unsigned count(const Json& value, unsigned maximum) {
    require(value.is_number_integer() &&
                (value.is_number_unsigned() || value.get<std::int64_t>() >= 0),
            "Surface count must be a nonnegative integer");
    const auto n = value.get<std::uint64_t>();
    require(n <= maximum, "Surface count exceeds limits");
    return unsigned(n);
}
bool planar(TextureDimension d) {
    return d == TextureDimension::D2 || d == TextureDimension::D2Array;
}
const char* texture_type(TextureDimension d) {
    switch (d) {
    case TextureDimension::D2:
        return "Texture2D";
    case TextureDimension::D2Array:
        return "Texture2DArray";
    case TextureDimension::Cube:
        return "TextureCube";
    case TextureDimension::CubeArray:
        return "TextureCubeArray";
    case TextureDimension::D3:
        return "Texture3D";
    }
    throw std::runtime_error("Unsupported surface texture dimension");
}
const char* reflected_dimension(TextureDimension d) {
    switch (d) {
    case TextureDimension::D2:
        return "texture2d";
    case TextureDimension::D2Array:
        return "texture2d_array";
    case TextureDimension::Cube:
        return "texture_cube";
    case TextureDimension::CubeArray:
        return "texture_cube_array";
    case TextureDimension::D3:
        return "texture3d";
    }
    throw std::runtime_error("Unsupported surface texture dimension");
}
std::string float_type(unsigned width) {
    return width == 1 ? "float" : "float" + std::to_string(width);
}
void uv_intent(const MaterialTextureSlot& slot, const SurfaceShaderDefinition& def) {
    if (planar(slot.dimension))
        require(std::find(def.uv_sets.begin(), def.uv_sets.end(), slot.uv_set) != def.uv_sets.end(),
                "Surface texture uses an undeclared UV set");
    else
        require(slot.uv_set == 0 && slot.offset == std::array<float, 2>{0, 0} &&
                    slot.scale == std::array<float, 2>{1, 1} && slot.rotation == 0,
                "Volume/cube surface textures take explicit coordinates; planar UV overrides are "
                "unsupported");
}
} // namespace
MaterialData surface_material_defaults(const SurfaceShaderDefinition& d) {
    MaterialData result;
    result.model = surface_material_model;
    result.parameters = d.parameters;
    result.textures = d.textures;
    return result;
}
void validate_surface_definition(const SurfaceShaderDefinition& d) {
    validate_material(surface_material_defaults(d));
    // Admission budgets, not universal hardware limits. Shader/PSO preparation
    // separately validates the actual enabled device/backend capabilities.
    require(d.uv_sets.size() <= 32, "Surface UV declaration count exceeds admission budget");
    std::set<unsigned> unique;
    for (const auto uv : d.uv_sets)
        require(unique.insert(uv).second, "Surface UV declarations must be unique");
    for (const auto& [name, parameter] : d.parameters) {
        (void)parameter;
        identifier(name);
    }
    for (const auto& [name, slot] : d.textures) {
        identifier(name);
        uv_intent(slot, d);
    }
}
Json surface_definition_document(const SurfaceShaderDefinition& d) {
    validate_surface_definition(d);
    const auto values = material_values_document(surface_material_defaults(d));
    return {{"version", 1},
            {"uv_sets", d.uv_sets},
            {"parameters", values.at("parameters")},
            {"textures", values.at("textures")}};
}
SurfaceShaderDefinition surface_definition(const Json& j) {
    require(j.is_object() && j.size() <= 16 && j.at("version").is_number_integer() &&
                j.at("version") == 1,
            "Unsupported surface interface version");
    require(j.at("uv_sets").is_array() && j.at("uv_sets").size() <= 32,
            "Surface UV declaration count exceeds admission budget");
    SurfaceShaderDefinition d;
    for (const auto& uv : j.at("uv_sets"))
        d.uv_sets.push_back(count(uv, UINT32_MAX));
    auto values = material_values_document(surface_material_defaults(d));
    values["parameters"] = j.at("parameters");
    values["textures"] = j.at("textures");
    auto parsed = material_values_from_document(values);
    d.parameters = std::move(parsed.parameters);
    d.textures = std::move(parsed.textures);
    validate_surface_definition(d);
    return d;
}
MaterialLayout surface_material_layout(const SurfaceShaderDefinition& d) {
    validate_surface_definition(d);
    MaterialLayout result;
    result.model = surface_material_model;
    for (const auto& [name, p] : d.parameters)
        result.parameters[name] = p.type;
    for (const auto& [name, t] : d.textures)
        result.textures[name] = {t.semantic, t.dimension, true};
    return result;
}
void validate_surface_material(const MaterialData& material, const SurfaceShaderDefinition& d) {
    validate_material_layout(material, surface_material_layout(d));
    for (const auto& [name, t] : material.textures) {
        (void)name;
        uv_intent(t, d);
    }
}
std::string surface_shader_header(const SurfaceShaderDefinition& d, bool emulated) {
    validate_surface_definition(d);
    const auto uv_count = std::max<std::size_t>(1, d.uv_sets.size());
    std::string s = "#ifndef FORGE_SURFACE_INTERFACE_V1\n#define FORGE_SURFACE_INTERFACE_V1\n"
                    "struct ForgeSurfaceInput {float4 Position:SV_Position;float3 World:TEXCOORD0;"
                    "float3 Normal:TEXCOORD1;float3 Tangent:TEXCOORD2;float3 Bitangent:TEXCOORD3;"
                    "float4 Color:COLOR0;nointerpolation float4 LegacyTint:COLOR1;float2 UV[" +
                    std::to_string(uv_count) +
                    "]:TEXCOORD4;bool FrontFace:SV_IsFrontFace;};\n"
                    "cbuffer ForgeSurfaceState {float4 g_ForgeSurfaceState;};\n";
    if (!d.parameters.empty()) {
        s += "cbuffer ForgeSurfaceMaterial {\n";
        for (const auto& [name, p] : d.parameters)
            s += float_type(material_parameter_width(p.type)) + " g_SurfaceParameter_" + name +
                 ";\n";
        s += "};\n";
        for (const auto& [name, p] : d.parameters)
            s += float_type(material_parameter_width(p.type)) + " ForgeParameter_" + name +
                 "(){return g_SurfaceParameter_" + name + ";}\n";
    }
    for (std::size_t i = 0; i < d.uv_sets.size(); ++i)
        s += "float2 ForgeUV" + std::to_string(d.uv_sets[i]) +
             "(ForgeSurfaceInput input){return input.UV[" + std::to_string(i) + "];}\n";
    if (!d.textures.empty()) {
        s += "cbuffer ForgeSurfaceUV {float4 g_SurfaceUvRows[" +
             std::to_string(d.textures.size() * 2) + "];};\n";
        if (!emulated)
            s += "SamplerState g_SurfaceSamplers[" + std::to_string(d.textures.size()) + "];\n";
    }
    unsigned i = 0;
    for (const auto& [name, slot] : d.textures) {
        const auto index = std::to_string(i);
        const auto sampler =
            emulated ? "g_SurfaceSamplers_" + index : "g_SurfaceSamplers[" + index + "]";
        if (emulated)
            s += "SamplerState " + sampler + ";\n";
        s +=
            std::string(texture_type(slot.dimension)) + "<float4> g_SurfaceTexture_" + name + ";\n";
        const auto width = slot.dimension == TextureDimension::D2          ? 2
                           : slot.dimension == TextureDimension::CubeArray ? 4
                                                                           : 3;
        s += "float4 ForgeSample_" + name + "(" + float_type(width) + " coordinate){\n";
        if (planar(slot.dimension))
            s += "float3 u=g_SurfaceUvRows[" + std::to_string(i * 2) + "].xyz,v=g_SurfaceUvRows[" +
                 std::to_string(i * 2 + 1) +
                 "].xyz;coordinate.xy=float2(dot(u.xy,coordinate.xy)+u.z,dot(v.xy,coordinate.xy)+v."
                 "z);\n";
        s += "return g_SurfaceTexture_" + name + ".Sample(" + sampler + ",coordinate);}\n";
        if (slot.dimension == TextureDimension::D2)
            s += "float4 ForgeSample_" + name + "(ForgeSurfaceInput input){return ForgeSample_" +
                 name + "(input.UV[(uint)g_SurfaceUvRows[" + std::to_string(i * 2) + "].w]);}\n";
        if (slot.dimension == TextureDimension::D2Array)
            s += "float4 ForgeSample_" + name +
                 "(ForgeSurfaceInput input,float layer){return ForgeSample_" + name +
                 "(float3(input.UV[(uint)g_SurfaceUvRows[" + std::to_string(i * 2) +
                 "].w],layer));}\n";
        ++i;
    }
    s += "#endif\n";
    return s;
}
std::string surface_shader_wrapper(std::string_view source, std::string_view function, bool depth) {
    identifier(function);
    require(!source.empty() && source.size() <= 1024 &&
                source.find_first_of("\"\r\n\\") == source.npos,
            "Invalid surface function source path");
    for (const unsigned char c : source)
        require(c >= 32 && c != 127, "Control character in surface function source path");
    std::string s =
        "#include \"engine/forge.surface.hlsli\"\n#include \"" + std::string(source) + "\"\n";
    s += depth ? "void ForgeSurfaceDepth(ForgeSurfaceInput input) {\n"
               : "float4 ForgeSurfaceColor(ForgeSurfaceInput input):SV_Target0 {\n";
    s += "float4 value=" + std::string(function) +
         "(input);\n"
         "if(!all(isfinite(value)))value=float4(1,0,1,1);\n"
         "if(g_ForgeSurfaceState.x==1 && value.a<g_ForgeSurfaceState.y)discard;\n";
    if (!depth)
        s += "if(g_ForgeSurfaceState.x!=2)value.a=1;return value;\n";
    return s + "}\n";
}
SurfaceBindingLayout surface_binding_layout(const SurfaceShaderDefinition& d,
                                            const Json& reflection) {
    validate_surface_definition(d);
    require(reflection.at("stage") == "pixel" && reflection.at("resources").is_array() &&
                reflection.at("resources").size() <= 256,
            "Invalid reflected surface stage");
    SurfaceBindingLayout result;
    std::set<std::string> names;
    for (const auto& resource : reflection.at("resources")) {
        const auto name = resource.at("name").get<std::string>();
        require(names.insert(name).second && count(resource.at("array_size"), 64) > 0,
                "Duplicate/invalid surface resource");
        const auto kind = resource.at("kind").get<std::string>();
        if (name == "g_SurfaceSamplers") {
            require(kind == "sampler", "Surface sampler has wrong reflected type");
            result.sampler_count = count(resource.at("array_size"), unsigned(d.textures.size()));
        } else if (name.starts_with("g_SurfaceTexture_")) {
            const auto role = name.substr(std::string_view("g_SurfaceTexture_").size());
            const auto found = d.textures.find(role);
            require(found != d.textures.end() && kind == "texture_srv" &&
                        resource.at("array_size") == 1 &&
                        resource.at("dimension") == reflected_dimension(found->second.dimension),
                    "Surface texture declaration/reflection mismatch");
            result.textures.push_back(role);
        } else if (name == "ForgeSurfaceMaterial" || name == "ForgeSurfaceState" ||
                   name == "ForgeSurfaceUV") {
            require(kind == "constant_buffer" && resource.at("array_size") == 1,
                    "Surface constant buffer has incompatible type/count");
            const auto bytes = count(resource.at("size"), 65536);
            require(bytes && bytes % 16 == 0, "Invalid surface constant buffer size");
            const auto& fields = resource.at("variables");
            require(fields.is_array() && fields.size() <= 256, "Invalid surface field count");
            if (name == "ForgeSurfaceMaterial") {
                result.parameter_bytes = bytes;
                std::set<unsigned> occupied;
                std::set<std::string> seen;
                for (const auto& field : fields) {
                    const auto variable = field.at("name").get<std::string>();
                    constexpr std::string_view prefix = "g_SurfaceParameter_";
                    require(variable.starts_with(prefix), "Unknown surface parameter variable");
                    const auto role = variable.substr(prefix.size());
                    const auto p = d.parameters.find(role);
                    require(p != d.parameters.end() && seen.insert(role).second,
                            "Undeclared/duplicate surface parameter");
                    const auto width = material_parameter_width(p->second.type);
                    const auto offset = count(field.at("offset"), bytes);
                    require(field.at("basic") == "float" && field.at("rows") == 1 &&
                                field.at("columns") == width &&
                                field.at("class") == (width == 1 ? "scalar" : "vector") &&
                                field.at("array_size") == 0 && field.at("members").empty() &&
                                offset % 4 == 0 && width * 4 <= bytes - offset,
                            "Surface parameter type/range differs from declaration");
                    for (unsigned at = offset; at < offset + width * 4; at += 4)
                        require(occupied.insert(at).second, "Overlapping surface parameters");
                    result.parameters.push_back({role, offset, width});
                }
            } else {
                const bool state = name == "ForgeSurfaceState";
                require(fields.size() == 1 && bytes == (state ? 16 : d.textures.size() * 32),
                        "Surface generated buffer size differs from declaration");
                const auto& f = fields[0];
                require(f.at("name") == (state ? "g_ForgeSurfaceState" : "g_SurfaceUvRows") &&
                            f.at("basic") == "float" && f.at("class") == "vector" &&
                            f.at("rows") == 1 && f.at("columns") == 4 && f.at("offset") == 0 &&
                            f.at("members").empty() &&
                            count(f.at("array_size"), 128) == (state ? 0 : d.textures.size() * 2),
                        "Surface generated buffer fields differ from declaration");
                if (state)
                    result.settings = true;
                else
                    result.uv_bytes = bytes;
            }
        } else
            throw std::runtime_error("Undeclared surface resource: " + name);
    }
    return result;
}
std::vector<std::byte> surface_parameter_bytes(const MaterialData& m,
                                               const SurfaceBindingLayout& l) {
    validate_material(m);
    require(l.parameter_bytes <= 65536, "Surface parameter buffer exceeds budget");
    std::vector<std::byte> bytes(l.parameter_bytes);
    for (const auto& p : l.parameters) {
        const auto& value = m.parameters.at(p.name);
        require(p.width == material_parameter_width(value.type) && p.offset <= bytes.size() &&
                    p.width * 4 <= bytes.size() - p.offset,
                "Surface parameter packing is incompatible");
        std::memcpy(bytes.data() + p.offset, value.value.data(), p.width * sizeof(float));
    }
    return bytes;
}
std::vector<std::array<float, 4>> surface_uv_rows(const MaterialData& m,
                                                  const SurfaceShaderDefinition& d) {
    validate_surface_material(m, d);
    std::vector<std::array<float, 4>> rows;
    for (const auto& [name, declared] : d.textures) {
        (void)declared;
        const auto& slot = m.textures.at(name);
        const auto c = std::cos(double(slot.rotation)), s = std::sin(double(slot.rotation));
        const float uv = planar(slot.dimension)
                             ? float(std::find(d.uv_sets.begin(), d.uv_sets.end(), slot.uv_set) -
                                     d.uv_sets.begin())
                             : 0;
        rows.push_back({float(c * slot.scale[0]), float(-s * slot.scale[1]), slot.offset[0], uv});
        rows.push_back({float(s * slot.scale[0]), float(c * slot.scale[1]), slot.offset[1], 0});
    }
    return rows;
}
} // namespace forge
