#include "cooked_envelope.hpp"
#include "mesh_morph.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <forge/mesh_asset.hpp>
#include <limits>
#include <map>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::byte, 8> magic{std::byte{'F'}, std::byte{'R'}, std::byte{'G'},
                                         std::byte{'M'}, std::byte{'E'}, std::byte{'S'},
                                         std::byte{'H'}, std::byte{0}};
constexpr std::size_t metadata_limit = 8 * 1024 * 1024;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void add(std::size_t& total, std::size_t count, std::size_t limit) {
    require(count <= limit && total <= limit - count, "Mesh aggregate limit exceeded");
    total += count;
}
bool named_set(std::string_view name, std::string_view prefix) {
    if (!name.starts_with(prefix) || name.size() == prefix.size())
        return false;
    const auto number = name.substr(prefix.size());
    return (number.size() == 1 || number[0] != '0') &&
           std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; });
}
void validate_streams(const std::vector<MeshStream>& streams, std::size_t vertices, bool morph,
                      MeshLimits limits, std::size_t& bytes) {
    require(!streams.empty() && streams.size() <= limits.streams, "Invalid mesh stream count");
    std::set<std::string> names;
    for (const auto& s : streams) {
        require(!s.semantic.empty() && s.semantic.size() <= 256 &&
                    s.semantic.find('\0') == std::string::npos && names.insert(s.semantic).second,
                "Invalid/duplicate mesh semantic");
        require(s.components && s.components <= 16 && vertices <= limits.vertices &&
                    s.scalar_count() / s.components == vertices &&
                    s.scalar_count() % s.components == 0,
                "Mesh stream shape disagrees with vertex count");
        require(s.scalar_count() <= limits.bytes / 4, "Mesh stream exceeds byte budget");
        add(bytes, s.scalar_count() * 4, limits.bytes);
        const auto* values = std::get_if<std::vector<float>>(&s.values);
        if (values)
            for (float x : *values)
                require(std::isfinite(x), "Non-finite mesh scalar");
        if (s.semantic == "POSITION" || s.semantic == "NORMAL")
            require(values && s.components == 3, "Position/normal requires float3");
        if (s.semantic == "TANGENT")
            require(values && s.components == (morph ? 3u : 4u), "Invalid tangent shape");
        if (named_set(s.semantic, "TEXCOORD_"))
            require(values && s.components == 2, "Texture coordinates require float2");
        if (named_set(s.semantic, "COLOR_"))
            require(values && (s.components == 3 || s.components == 4),
                    "Invalid vertex color shape");
        if (named_set(s.semantic, "JOINTS_"))
            require(!morph && !values && s.components == 4, "Joints require uint4");
        if (named_set(s.semantic, "WEIGHTS_")) {
            require(!morph && values && s.components == 4, "Weights require float4");
            for (float x : *values)
                require(x >= 0, "Invalid skin weight");
        }
        if (!morph && (s.semantic == "NORMAL" || s.semantic == "TANGENT"))
            for (std::size_t i = 0; i < vertices; ++i) {
                const auto p = i * s.components;
                require(std::abs(std::hypot(double((*values)[p]), double((*values)[p + 1]),
                                            double((*values)[p + 2])) -
                                 1) <= .001,
                        "Mesh normal/tangent is not normalized");
                if (s.semantic == "TANGENT")
                    require(std::abs((*values)[p + 3]) == 1, "Invalid tangent handedness");
            }
    }
    if (!morph) {
        require(names.contains("POSITION"), "Mesh has no POSITION stream");
        for (const auto& name : names) {
            if (named_set(name, "JOINTS_"))
                require(names.contains("WEIGHTS_" + name.substr(7)), "Joints have no weights");
            if (named_set(name, "WEIGHTS_"))
                require(names.contains("JOINTS_" + name.substr(8)), "Weights have no joints");
        }
    }
}
void write32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(std::byte((value >> (i * 8)) & 255));
}
std::uint32_t read32(std::span<const std::byte> bytes, std::size_t& at) {
    require(at <= bytes.size() && bytes.size() - at >= 4, "Truncated mesh scalar");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[at++]) << (i * 8);
    return value;
}
std::size_t count(const Json& j, std::size_t maximum) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Mesh count must be nonnegative integer");
    const auto result = j.get<std::uint64_t>();
    require(result <= maximum, "Mesh count exceeds limit");
    return static_cast<std::size_t>(result);
}
const Json& array(const Json& j, std::size_t maximum) {
    require(j.is_array() && j.size() <= maximum, "Invalid mesh metadata array");
    return j;
}
float scalar(const Json& j) {
    require(j.is_number(), "Mesh metadata scalar must be numeric");
    const auto value = j.get<double>();
    require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
            "Invalid mesh metadata scalar");
    const auto result = static_cast<float>(value);
    require(value == 0 || result != 0, "Mesh metadata scalar underflows float");
    return result;
}
} // namespace
std::size_t MeshStream::scalar_count() const {
    return std::visit([](const auto& x) { return x.size(); }, values);
}
const MeshStream* MeshPart::find(std::string_view name) const {
    auto found = std::find_if(streams.begin(), streams.end(),
                              [&](const auto& s) { return s.semantic == name; });
    return found == streams.end() ? nullptr : &*found;
}
std::size_t MeshData::byte_size() const {
    std::size_t bytes = 0;
    auto streams = [&](const auto& list) {
        for (const auto& s : list) {
            require(s.scalar_count() <= SIZE_MAX / 4, "Mesh byte size overflow");
            add(bytes, s.scalar_count() * 4, SIZE_MAX);
        }
    };
    for (const auto& lod : lods)
        for (const auto& part : lod.parts) {
            require(part.indices.size() <= SIZE_MAX / 4, "Mesh index size overflow");
            add(bytes, part.indices.size() * 4, SIZE_MAX);
            require(part.joint_palette.size() <= SIZE_MAX / sizeof(std::uint32_t),
                    "Mesh palette size overflow");
            add(bytes, part.joint_palette.size() * sizeof(std::uint32_t), SIZE_MAX);
            streams(part.streams);
            for (const auto& morph : part.morph_targets)
                streams(morph);
        }
    return bytes;
}
std::size_t MeshData::resident_bytes() const {
    std::size_t bytes = sizeof(MeshData);
    auto storage = [&](std::size_t count, std::size_t width) {
        require(count <= SIZE_MAX / width, "Mesh allocation estimate overflow");
        add(bytes, count * width, SIZE_MAX);
    };
    auto streams = [&](const std::vector<MeshStream>& list) {
        storage(list.capacity(), sizeof(MeshStream));
        for (const auto& s : list) {
            storage(s.semantic.capacity() + 1, 1);
            std::visit([&](const auto& values) { storage(values.capacity(), 4); }, s.values);
        }
    };
    storage(lods.capacity(), sizeof(MeshLod));
    for (const auto& lod : lods) {
        storage(lod.parts.capacity(), sizeof(MeshPart));
        for (const auto& part : lod.parts) {
            storage(part.indices.capacity(), 4);
            storage(part.joint_palette.capacity(), 4);
            streams(part.streams);
            storage(part.morph_targets.capacity(), sizeof(std::vector<MeshStream>));
            for (const auto& target : part.morph_targets)
                streams(target);
        }
    }
    storage(morph_defaults.capacity(), 4);
    storage(morph_names.capacity(), sizeof(std::string));
    for (const auto& name : morph_names)
        storage(name.capacity() + 1, 1);
    return bytes;
}
MeshBounds mesh_bounds(const MeshPart& part) {
    const auto* stream = part.find("POSITION");
    require(stream && stream->components == 3 && part.vertices &&
                std::holds_alternative<std::vector<float>>(stream->values),
            "Invalid mesh positions");
    const auto& values = std::get<std::vector<float>>(stream->values);
    require(values.size() / 3 == part.vertices && values.size() % 3 == 0, "Invalid position count");
    MeshBounds result;
    std::copy_n(values.begin(), 3, result.minimum.begin());
    result.maximum = result.minimum;
    for (std::size_t i = 0; i < values.size(); ++i) {
        require(std::isfinite(values[i]), "Non-finite mesh position");
        result.minimum[i % 3] = std::min(result.minimum[i % 3], values[i]);
        result.maximum[i % 3] = std::max(result.maximum[i % 3], values[i]);
    }
    return result;
}
MeshBounds morph_bounds(const MeshPart& part, std::span<const float> weights) {
    require(weights.size() == part.morph_targets.size() && weights.size() <= 256,
            "Morph bounds weight count differs from targets");
    std::array<double, 3> minimum, maximum, magnitude;
    unsigned operations = 0;
    for (unsigned c = 0; c < 3; ++c) {
        minimum[c] = part.bounds.minimum[c];
        maximum[c] = part.bounds.maximum[c];
        magnitude[c] = std::max(std::abs(minimum[c]), std::abs(maximum[c]));
        require(std::isfinite(minimum[c]) && std::isfinite(maximum[c]) && minimum[c] <= maximum[c],
                "Morph bounds require valid base bounds");
    }
    for (std::size_t t = 0; t < weights.size(); ++t) {
        const double weight = weights[t];
        require(std::isfinite(weight), "Nonfinite morph bounds weight");
        if (weight == 0)
            continue;
        for (const auto& stream : part.morph_targets[t]) {
            if (stream.semantic != "POSITION")
                continue;
            require(stream.components == 3 &&
                        std::holds_alternative<std::vector<float>>(stream.values),
                    "Invalid position morph layout");
            const auto& values = std::get<std::vector<float>>(stream.values);
            require(values.size() == std::size_t(part.vertices) * 3 && !values.empty(),
                    "Invalid position morph count");
            std::array<double, 3> lo{values[0], values[1], values[2]}, hi = lo;
            for (std::size_t i = 0; i < values.size(); ++i) {
                require(std::isfinite(values[i]), "Nonfinite position morph delta");
                lo[i % 3] = std::min(lo[i % 3], double(values[i]));
                hi[i % 3] = std::max(hi[i % 3], double(values[i]));
            }
            operations += 2; // One product and one sum per shader channel.
            for (unsigned c = 0; c < 3; ++c) {
                magnitude[c] += std::abs(weight) * std::max(std::abs(lo[c]), std::abs(hi[c]));
                minimum[c] += weight * (weight > 0 ? lo[c] : hi[c]);
                maximum[c] += weight * (weight > 0 ? hi[c] : lo[c]);
            }
        }
    }
    MeshBounds result;
    const double accumulated = operations * double(std::numeric_limits<float>::epsilon());
    const double gamma = accumulated / (1 - accumulated);
    for (unsigned c = 0; c < 3; ++c) {
        // Include the forward-error bound for float shader accumulation, not
        // just rounding of the final double interval endpoints. FMA is no worse.
        minimum[c] -= gamma * magnitude[c];
        maximum[c] += gamma * magnitude[c];
        const double limit = std::numeric_limits<float>::max();
        require(std::isfinite(minimum[c]) && std::isfinite(maximum[c]) && minimum[c] >= -limit &&
                    maximum[c] <= limit,
                "Morphed positions exceed the finite GPU profile");
        result.minimum[c] = float(minimum[c]);
        result.maximum[c] = float(maximum[c]);
        if (result.minimum[c] > minimum[c])
            result.minimum[c] = std::nextafter(result.minimum[c], -INFINITY);
        if (result.maximum[c] < maximum[c])
            result.maximum[c] = std::nextafter(result.maximum[c], INFINITY);
    }
    return result;
}
void validate_mesh(const MeshData& mesh, MeshLimits limits) {
    require(mesh.material_slots && mesh.material_slots <= 65536 && !mesh.lods.empty() &&
                mesh.lods.size() <= limits.lods &&
                mesh.morph_names.size() <= limits.morph_targets &&
                mesh.morph_names.size() == mesh.morph_defaults.size(),
            "Invalid mesh header");
    for (std::size_t i = 0; i < mesh.morph_names.size(); ++i)
        require(mesh.morph_names[i].size() <= 256 &&
                    mesh.morph_names[i].find('\0') == std::string::npos &&
                    std::isfinite(mesh.morph_defaults[i]),
                "Invalid morph metadata");
    std::size_t bytes = 0, vertices = 0, indices = 0, parts = 0;
    float previous = 2;
    for (const auto& lod : mesh.lods) {
        require(std::isfinite(lod.screen_coverage) && lod.screen_coverage >= 0 &&
                    lod.screen_coverage <= 1 && lod.screen_coverage < previous &&
                    !lod.parts.empty(),
                "Invalid LOD ordering/coverage");
        if (previous == 2)
            require(lod.screen_coverage == 1, "First LOD must start at full coverage");
        previous = lod.screen_coverage;
        add(parts, lod.parts.size(), limits.parts);
        for (const auto& part : lod.parts) {
            require(part.vertices && part.material_slot < mesh.material_slots &&
                        part.morph_targets.size() == mesh.morph_names.size(),
                    "Invalid mesh part");
            add(vertices, part.vertices, limits.vertices);
            validate_streams(part.streams, part.vertices, false, limits, bytes);
            require(part.joint_palette.size() <= 256, "Mesh joint palette exceeds draw profile");
            if (!part.joint_palette.empty()) {
                std::set<std::uint32_t> joints;
                for (const auto joint : part.joint_palette)
                    require(joint < 65536 && joints.insert(joint).second,
                            "Invalid/duplicate mesh palette joint");
                add(bytes, part.joint_palette.size() * sizeof(std::uint32_t), limits.bytes);
                const auto* indices = part.find("JOINTS_0");
                const auto* weights = part.find("WEIGHTS_0");
                require(indices && weights, "Prepared skin needs joint/weight streams");
                for (const auto& stream : part.streams)
                    if (named_set(stream.semantic, "JOINTS_") ||
                        named_set(stream.semantic, "WEIGHTS_"))
                        require(stream.semantic == "JOINTS_0" || stream.semantic == "WEIGHTS_0",
                                "Prepared skin contains additional influence streams");
                const auto& j = std::get<std::vector<std::uint32_t>>(indices->values);
                const auto& w = std::get<std::vector<float>>(weights->values);
                for (std::size_t vertex = 0; vertex < part.vertices; ++vertex) {
                    double sum = 0;
                    for (unsigned slot = 0; slot < 4; ++slot) {
                        const auto at = vertex * 4 + slot;
                        require(w[at] <= 1, "Prepared skin weight exceeds one");
                        require(j[at] < part.joint_palette.size(),
                                "Prepared skin joint exceeds palette");
                        if (w[at] > 0)
                            for (unsigned prior = 0; prior < slot; ++prior)
                                require(w[vertex * 4 + prior] == 0 ||
                                            j[vertex * 4 + prior] != j[at],
                                        "Duplicate active prepared skin joint");
                        sum += w[at];
                    }
                    require(std::abs(sum - 1) <= 1e-5, "Prepared skin weights are not normalized");
                }
            }
            for (const auto& target : part.morph_targets) {
                validate_streams(target, part.vertices, true, limits, bytes);
                for (const auto& delta : target) {
                    const auto* base = part.find(delta.semantic);
                    require(base != nullptr, "Morph delta requires a matching base vertex stream");
                    require(
                        base->values.index() == delta.values.index() &&
                            (delta.semantic == "TANGENT" || base->components == delta.components),
                        "Morph delta scalar/layout disagrees with its base stream");
                }
            }
            const auto width = part.topology == MeshTopology::Points      ? 1u
                               : part.topology == MeshTopology::Lines     ? 2u
                               : part.topology == MeshTopology::Triangles ? 3u
                                                                          : 0u;
            require(width && !part.indices.empty() && part.indices.size() % width == 0,
                    "Invalid mesh primitive topology/count");
            add(indices, part.indices.size(), limits.indices);
            require(part.indices.size() <= limits.bytes / 4, "Mesh indices exceed byte budget");
            add(bytes, part.indices.size() * 4, limits.bytes);
            for (auto index : part.indices)
                require(index < part.vertices, "Mesh index outside vertices");
            require(part.bounds == mesh_bounds(part), "Mesh bounds disagree with positions");
        }
    }
}
std::vector<std::byte> encode_mesh(const MeshData& mesh, MeshLimits limits) {
    validate_mesh(mesh, limits);
    std::vector<std::byte> payload;
    payload.reserve(mesh.byte_size());
    auto stream_json = [&](const std::vector<MeshStream>& streams) {
        Json result = Json::array();
        std::vector<const MeshStream*> ordered;
        for (const auto& s : streams)
            ordered.push_back(&s);
        std::sort(ordered.begin(), ordered.end(),
                  [](auto a, auto b) { return a->semantic < b->semantic; });
        for (auto s : ordered) {
            result.push_back({{"semantic", s->semantic},
                              {"components", s->components},
                              {"type", s->values.index() == 0 ? "f32" : "u32"},
                              {"offset", payload.size()},
                              {"count", s->scalar_count()}});
            std::visit(
                [&](const auto& values) {
                    for (auto value : values) {
                        if constexpr (std::is_same_v<decltype(value), float>)
                            write32(payload,
                                    std::bit_cast<std::uint32_t>(value == 0 ? 0.f : value));
                        else
                            write32(payload, value);
                    }
                },
                s->values);
        }
        return result;
    };
    Json metadata{{"material_slots", mesh.material_slots},
                  {"morph_names", mesh.morph_names},
                  {"morph_defaults", mesh.morph_defaults},
                  {"lods", Json::array()}};
    bool prepared_skin = false;
    for (const auto& lod : mesh.lods) {
        Json parts = Json::array();
        for (const auto& p : lod.parts) {
            Json part{{"vertices", p.vertices},           {"material_slot", p.material_slot},
                      {"topology", unsigned(p.topology)}, {"minimum", p.bounds.minimum},
                      {"maximum", p.bounds.maximum},      {"streams", stream_json(p.streams)},
                      {"morph_targets", Json::array()}};
            if (!p.joint_palette.empty()) {
                part["joint_palette"] = p.joint_palette;
                prepared_skin = true;
            }
            for (const auto& target : p.morph_targets)
                part["morph_targets"].push_back(stream_json(target));
            part["indices"] = {{"offset", payload.size()}, {"count", p.indices.size()}};
            for (auto index : p.indices)
                write32(payload, index);
            parts.push_back(std::move(part));
        }
        metadata["lods"].push_back(
            {{"coverage", lod.screen_coverage}, {"parts", std::move(parts)}});
    }
    return asset_detail::encode_envelope(metadata, payload, magic, metadata_limit,
                                         prepared_skin ? 2 : 1);
}

MeshData decode_mesh(std::span<const std::byte> bytes, MeshLimits limits) {
    const auto envelope =
        asset_detail::decode_envelope(bytes, magic, metadata_limit, limits.bytes, 2);
    const auto& metadata = envelope.metadata;
    const auto payload = envelope.payload;
    std::size_t at = 0;
    auto streams = [&](const Json& list, std::size_t vertices) {
        std::vector<MeshStream> result;
        for (const auto& j : array(list, limits.streams)) {
            MeshStream s;
            s.semantic = j.at("semantic").get<std::string>();
            s.components = static_cast<unsigned>(count(j.at("components"), 16));
            const auto n = count(j.at("count"), payload.size() / 4);
            require(s.components && n / s.components == vertices && n % s.components == 0 &&
                        count(j.at("offset"), payload.size()) == at &&
                        n <= (payload.size() - at) / 4,
                    "Invalid mesh stream span");
            const auto type = j.at("type").get<std::string>();
            require(type == "f32" || type == "u32", "Unsupported cooked mesh scalar type");
            if (type == "f32") {
                std::vector<float> values;
                values.reserve(n);
                for (std::size_t i = 0; i < n; ++i)
                    values.push_back(std::bit_cast<float>(read32(payload, at)));
                s.values = std::move(values);
            } else {
                std::vector<std::uint32_t> values;
                values.reserve(n);
                for (std::size_t i = 0; i < n; ++i)
                    values.push_back(read32(payload, at));
                s.values = std::move(values);
            }
            result.push_back(std::move(s));
        }
        return result;
    };
    MeshData result;
    result.material_slots = static_cast<std::uint32_t>(count(metadata.at("material_slots"), 65536));
    for (const auto& name : array(metadata.at("morph_names"), limits.morph_targets))
        result.morph_names.push_back(name.get<std::string>());
    for (const auto& value : array(metadata.at("morph_defaults"), limits.morph_targets))
        result.morph_defaults.push_back(scalar(value));
    std::size_t vertices = 0, indices = 0, parts = 0;
    bool prepared_skin = false;
    for (const auto& l : array(metadata.at("lods"), limits.lods)) {
        MeshLod lod;
        lod.screen_coverage = scalar(l.at("coverage"));
        for (const auto& p : array(l.at("parts"), limits.parts)) {
            add(parts, 1, limits.parts);
            MeshPart part;
            part.vertices = static_cast<std::uint32_t>(
                count(p.at("vertices"), std::min<std::size_t>(limits.vertices, UINT32_MAX)));
            add(vertices, part.vertices, limits.vertices);
            part.material_slot = static_cast<std::uint32_t>(count(p.at("material_slot"), 65535));
            part.topology = static_cast<MeshTopology>(count(p.at("topology"), 2));
            auto bounds = [&](const Json& j, auto& target) {
                require(array(j, 3).size() == 3, "Invalid mesh bounds shape");
                for (unsigned i = 0; i < 3; ++i)
                    target[i] = scalar(j[i]);
            };
            bounds(p.at("minimum"), part.bounds.minimum);
            bounds(p.at("maximum"), part.bounds.maximum);
            if (p.contains("joint_palette")) {
                require(envelope.version == 2 && !p.at("joint_palette").empty(),
                        "Joint palette requires cooked mesh version2");
                for (const auto& joint : array(p.at("joint_palette"), 256))
                    part.joint_palette.push_back(static_cast<std::uint32_t>(count(joint, 65535)));
                prepared_skin = true;
            }
            part.streams = streams(p.at("streams"), part.vertices);
            for (const auto& target : array(p.at("morph_targets"), limits.morph_targets))
                part.morph_targets.push_back(streams(target, part.vertices));
            const auto& index = p.at("indices");
            const auto n = count(index.at("count"), limits.indices);
            require(count(index.at("offset"), payload.size()) == at &&
                        n <= (payload.size() - at) / 4,
                    "Invalid mesh index span");
            add(indices, n, limits.indices);
            part.indices.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                part.indices.push_back(read32(payload, at));
            lod.parts.push_back(std::move(part));
        }
        result.lods.push_back(std::move(lod));
    }
    require(at == payload.size(), "Unexpected trailing cooked mesh bytes");
    require(envelope.version == (prepared_skin ? 2u : 1u),
            "Cooked mesh version does not match its skin features");
    validate_mesh(result, limits);
    return result;
}
} // namespace forge
