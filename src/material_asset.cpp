#include "cooked_envelope.hpp"
#include <cmath>
#include <forge/material_asset.hpp>
#include <limits>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::byte, 8> magic{std::byte{'F'}, std::byte{'R'}, std::byte{'G'},
                                         std::byte{'M'}, std::byte{'A'}, std::byte{'T'},
                                         std::byte{0},   std::byte{0}};
constexpr std::size_t metadata_limit = 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void key(std::string_view value) {
    require(!value.empty() && value.size() <= 256, "Invalid material key length");
    for (const unsigned char c : value)
        require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '.' || c == '-',
                "Invalid material key character");
}
unsigned number(const Json& j, unsigned maximum) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Material count/enum must be nonnegative integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= maximum, "Material count/enum exceeds bounds");
    return static_cast<unsigned>(n);
}
float scalar(const Json& j) {
    require(j.is_number(), "Material scalar is not numeric");
    const auto x = j.get<double>();
    require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(),
            "Material scalar is not finite/representable");
    const auto result = static_cast<float>(x);
    require(x == 0 || result != 0, "Material scalar underflow");
    return result == 0 ? 0 : result;
}
template <std::size_t N> std::array<float, N> vector(const Json& j) {
    require(j.is_array() && j.size() == N, "Invalid material vector width");
    std::array<float, N> result;
    for (std::size_t i = 0; i < N; ++i)
        result[i] = scalar(j[i]);
    return result;
}
const Json& entries(const Json& j, std::size_t limit) {
    require(j.is_object() && j.size() <= limit, "Material field count exceeds bounds");
    return j;
}
} // namespace
unsigned material_parameter_width(MaterialParameterType type) {
    switch (type) {
    case MaterialParameterType::Scalar:
        return 1;
    case MaterialParameterType::Vector2:
        return 2;
    case MaterialParameterType::Vector3:
    case MaterialParameterType::LinearColor3:
        return 3;
    case MaterialParameterType::Vector4:
    case MaterialParameterType::LinearColor4:
        return 4;
    }
    throw std::runtime_error("Unknown material parameter type");
}
void validate_material(const MaterialData& m) {
    key(m.model);
    require(unsigned(m.alpha) <= unsigned(MaterialAlpha::Blend) && std::isfinite(m.alpha_cutoff) &&
                m.alpha_cutoff >= 0,
            "Invalid material alpha state");
    require(m.parameters.size() <= 256 && m.textures.size() <= 64,
            "Material field count exceeds bounds");
    for (const auto& [name, p] : m.parameters) {
        key(name);
        require(!m.textures.contains(name), "Material parameter and texture keys overlap");
        const auto width = material_parameter_width(p.type);
        for (unsigned i = 0; i < 4; ++i)
            require(std::isfinite(p.value[i]) && (i < width || p.value[i] == 0),
                    "Invalid material parameter value/unused lane");
    }
    for (const auto& [name, t] : m.textures) {
        key(name);
        require(unsigned(t.semantic) <= unsigned(TextureSemantic::HdrColor) &&
                    unsigned(t.dimension) <= unsigned(TextureDimension::D3),
                "Invalid material texture kind");
        validate_sampler(t.sampler);
        for (const auto x : t.offset)
            require(std::isfinite(x), "Invalid texture offset");
        for (const auto x : t.scale)
            require(std::isfinite(x), "Invalid texture scale");
        require(std::isfinite(t.rotation), "Invalid texture rotation");
    }
}
void validate_material_bindings(const MaterialData& m, const MaterialTextureBindings& bindings) {
    validate_material(m);
    require(bindings.size() == m.textures.size(), "Material texture binding count mismatch");
    for (const auto& [name, ref] : bindings)
        require(m.textures.contains(name) && bool(ref.id),
                "Missing/unknown material texture binding");
}
void validate_material_layout(const MaterialData& m, const MaterialLayout& layout) {
    validate_material(m);
    key(layout.model);
    require(layout.model == m.model && layout.parameters.size() <= 256 &&
                layout.textures.size() <= 64,
            "Material model/layout mismatch");
    require(layout.parameters.size() == m.parameters.size(), "Material parameter layout changed");
    for (const auto& [name, type] : layout.parameters) {
        key(name);
        (void)material_parameter_width(type);
        const auto p = m.parameters.find(name);
        require(p != m.parameters.end() && p->second.type == type,
                "Material parameter type changed");
    }
    for (const auto& [name, t] : layout.textures) {
        key(name);
        require(!layout.parameters.contains(name) && unsigned(t.semantic) <= 3 &&
                    unsigned(t.dimension) <= 4,
                "Invalid material texture layout");
        require(!t.required || m.textures.contains(name), "Required material texture is missing");
    }
    for (const auto& [name, texture] : m.textures) {
        const auto t = layout.textures.find(name);
        require(t != layout.textures.end() && t->second.semantic == texture.semantic &&
                    t->second.dimension == texture.dimension,
                "Material texture layout changed");
    }
}
std::size_t MaterialData::resident_bytes() const {
    // Includes a conservative ordered-map node allowance; not allocator accounting.
    std::size_t bytes = sizeof(*this) + model.capacity();
    for (const auto& [name, p] : parameters) {
        (void)p;
        bytes += sizeof(std::pair<const std::string, MaterialParameter>) + 4 * sizeof(void*) +
                 name.capacity();
    }
    for (const auto& [name, t] : textures) {
        (void)t;
        bytes += sizeof(std::pair<const std::string, MaterialTextureSlot>) + 4 * sizeof(void*) +
                 name.capacity();
    }
    return bytes;
}
nlohmann::json material_values_document(const MaterialData& m) {
    validate_material(m);
    Json parameters = Json::object(), textures = Json::object();
    for (const auto& [name, p] : m.parameters) {
        const auto width = material_parameter_width(p.type);
        parameters[name] = {
            {"type", unsigned(p.type)},
            {"value", std::vector<float>(p.value.begin(), p.value.begin() + width)}};
    }
    for (const auto& [name, t] : m.textures)
        textures[name] = {{"semantic", unsigned(t.semantic)},
                          {"dimension", unsigned(t.dimension)},
                          {"sampler", t.sampler},
                          {"uv_set", t.uv_set},
                          {"offset", t.offset},
                          {"scale", t.scale},
                          {"rotation", t.rotation}};
    return {{"model", m.model},
            {"alpha", unsigned(m.alpha)},
            {"alpha_cutoff", m.alpha_cutoff},
            {"double_sided", m.double_sided},
            {"depth_test", m.depth_test},
            {"depth_write", m.depth_write},
            {"parameters", parameters},
            {"textures", textures}};
}
MaterialData material_values_from_document(const nlohmann::json& j) {
    MaterialData m;
    m.model = j.at("model").get<std::string>();
    m.alpha = MaterialAlpha(number(j.at("alpha"), 2));
    m.alpha_cutoff = scalar(j.at("alpha_cutoff"));
    m.double_sided = j.at("double_sided").get<bool>();
    m.depth_test = j.at("depth_test").get<bool>();
    m.depth_write = j.at("depth_write").get<bool>();
    for (const auto& [name, value] : entries(j.at("parameters"), 256).items()) {
        MaterialParameter p;
        p.type = MaterialParameterType(number(value.at("type"), 5));
        const auto width = material_parameter_width(p.type);
        const auto& v = value.at("value");
        require(v.is_array() && v.size() == width, "Material parameter width mismatch");
        for (unsigned i = 0; i < width; ++i)
            p.value[i] = scalar(v[i]);
        m.parameters.emplace(name, p);
    }
    for (const auto& [name, value] : entries(j.at("textures"), 64).items()) {
        MaterialTextureSlot t;
        t.semantic = TextureSemantic(number(value.at("semantic"), 3));
        t.dimension = TextureDimension(number(value.at("dimension"), 4));
        t.sampler = value.at("sampler").get<SamplerState>();
        t.uv_set = number(value.at("uv_set"), UINT32_MAX);
        t.offset = vector<2>(value.at("offset"));
        t.scale = vector<2>(value.at("scale"));
        t.rotation = scalar(value.at("rotation"));
        m.textures.emplace(name, t);
    }
    validate_material(m);
    return m;
}
std::vector<std::byte> encode_material(const MaterialData& m) {
    return asset_detail::encode_envelope(material_values_document(m), {}, magic, metadata_limit);
}
MaterialData decode_material(std::span<const std::byte> bytes) {
    return material_values_from_document(
        asset_detail::decode_envelope(bytes, magic, metadata_limit, 0).metadata);
}
} // namespace forge
