#include "bounded_json.hpp"
#include <algorithm>
#include <cmath>
#include <forge/material_graph.hpp>
#include <functional>
#include <iomanip>
#include <sstream>
namespace forge {
namespace {
using Json = nlohmann::json;
using Type = MaterialParameterType;
void require(bool ok, std::string message) {
    if (!ok)
        throw std::runtime_error("Material graph: " + message);
}
[[noreturn]] void fail(GraphNodeId node, std::string port, std::string code, std::string message) {
    throw MaterialGraphError({std::move(code), std::move(message), std::move(port), node});
}
const std::map<std::string, Type> types{
    {"scalar", Type::Scalar},   {"vector2", Type::Vector2},     {"vector3", Type::Vector3},
    {"vector4", Type::Vector4}, {"color3", Type::LinearColor3}, {"color4", Type::LinearColor4}};
unsigned uv_index(const Json& data) {
    if (!data.contains("uv_set"))
        return 0;
    const auto& v = data.at("uv_set");
    require(v.is_number_integer() && (v.is_number_unsigned() || v.get<int64_t>() >= 0),
            "UV set must be a nonnegative integer");
    const auto n = v.get<uint64_t>();
    require(n <= UINT32_MAX, "UV set exceeds logical index representation");
    return unsigned(n);
}
void persistable(const Json& j, unsigned depth = 0) {
    require(depth <= 64, "Graph JSON nesting exceeds admission budget");
    if (j.is_number_float())
        require(std::isfinite(j.get<double>()), "Nonfinite JSON value cannot be persisted");
    if (j.is_structured())
        for (const auto& v : j)
            persistable(v, depth + 1);
}
Type type(const Json& node) {
    const auto name = node.at("data").value("type", std::string("scalar"));
    require(types.contains(name), "Unknown value type " + name);
    return types.at(name);
}
std::string hlsl_type(Type t) {
    const auto width = material_parameter_width(t);
    return width == 1 ? "float" : "float" + std::to_string(width);
}
std::string identifier(const Json& node) {
    const auto key = node.at("data").at("key").get<std::string>();
    require(!key.empty() && key.size() <= 128, "Invalid binding key");
    require(std::all_of(key.begin(), key.end(),
                        [](unsigned char c) {
                            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                   (c >= '0' && c <= '9') || c == '_';
                        }) &&
                !(key[0] >= '0' && key[0] <= '9'),
            "Binding keys use ASCII identifiers");
    return key;
}
MaterialParameter value(const Json& node) {
    MaterialParameter p;
    p.type = type(node);
    const auto& a = node.at("data").at("value");
    require(a.is_array() && a.size() == material_parameter_width(p.type), "Wrong constant width");
    for (size_t i = 0; i < a.size(); ++i) {
        require(a[i].is_number(), "Constant must be numeric");
        double x = a[i].get<double>();
        float f = float(x);
        require(std::isfinite(x) && std::isfinite(f) && (x == 0 || f != 0),
                "Constant outside finite GPU representation");
        p.value[i] = f;
    }
    return p;
}
std::string literal(const MaterialParameter& p) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::scientific << std::setprecision(9);
    const auto n = material_parameter_width(p.type);
    if (n > 1)
        s << hlsl_type(p.type) << '(';
    for (unsigned i = 0; i < n; ++i) {
        if (i)
            s << ',';
        s << p.value[i];
    }
    if (n > 1)
        s << ')';
    return s.str();
}
std::string kind(const Json& node) { return node.at("type").get<std::string>(); }
MaterialGraphPort port(std::string key, Type t) { return {key, key, t}; }
const std::vector<MaterialGraphPort> surface_ports{
    port("base_color", Type::LinearColor4), port("metallic", Type::Scalar),
    port("roughness", Type::Scalar),        port("normal", Type::Vector3),
    port("emissive", Type::LinearColor3),   port("occlusion", Type::Scalar)};
const MaterialGraphPort& find_port(const std::vector<MaterialGraphPort>& ports,
                                   std::string_view key, GraphNodeId node) {
    const auto it =
        std::find_if(ports.begin(), ports.end(), [&](const auto& p) { return p.key == key; });
    if (it == ports.end())
        fail(node, std::string(key), "unknown_port", "Port is absent from this node schema");
    return *it;
}
struct Expression {
    Type type;
    std::string value;
};
class Compiler {
    const Json& graph_;
    std::map<GraphNodeId, const Json*> nodes_;
    std::map<std::pair<GraphNodeId, std::string>, const Json*> edges_;
    std::map<GraphNodeId, Expression> ready_;
    std::set<GraphNodeId> visiting_;
    unsigned temporary_ = 0;
    std::string body_;
    CompiledMaterialGraph result_;
    Expression input(GraphNodeId id, const MaterialGraphPort& p, std::string fallback) {
        const auto edge = edges_.find({id, p.key});
        if (edge == edges_.end())
            return {p.type, std::move(fallback)};
        const auto& from = edge->second->at("from");
        const auto source = from.at("node").get<GraphNodeId>();
        const auto output = from.at("port").get<std::string>();
        auto outputs = material_graph_outputs(*nodes_.at(source));
        const auto& schema = find_port(outputs, output, source);
        if (schema.type != p.type)
            fail(id, p.key, "type_mismatch",
                 "Connect matching port types or insert an explicit Convert node");
        return expression(source);
    }
    Expression expression(GraphNodeId id) {
        try {
            return evaluate(id);
        } catch (const MaterialGraphError&) {
            throw;
        } catch (const std::exception& e) {
            fail(id, "", "invalid_configuration", e.what());
        }
    }
    Expression evaluate(GraphNodeId id) {
        if (const auto found = ready_.find(id); found != ready_.end())
            return found->second;
        if (visiting_.size() >= 128)
            fail(id, "", "depth_limit", "Material graph evaluation exceeds 128 nested nodes");
        if (!visiting_.insert(id).second)
            fail(id, "", "cycle", "Material dataflow must be acyclic");
        const auto& node = *nodes_.at(id);
        const auto key = kind(node);
        const auto& schemas = material_graph_node_schemas();
        const auto known = std::find_if(schemas.begin(), schemas.end(),
                                        [&](const auto& s) { return s.key == key; });
        if (known == schemas.end() || node.at("version") != known->version)
            fail(id, "", "unavailable_schema",
                 "Node type/version is unavailable; its source data is preserved");
        auto outputs = material_graph_outputs(node);
        if (outputs.empty())
            fail(id, "", "invalid_output", "Surface Output cannot be used as an input");
        const auto t = outputs.front().type;
        std::string expression;
        const auto inputs = material_graph_inputs(node);
        auto arg = [&](size_t n, std::string fallback = "") {
            return input(id, inputs.at(n),
                         fallback.empty() ? "((" + hlsl_type(inputs.at(n).type) + ")0.0)"
                                          : std::move(fallback))
                .value;
        };
        if (key == "constant")
            expression = literal(value(node));
        else if (key == "parameter") {
            const auto binding = identifier(node);
            const auto p = value(node);
            if (!result_.surface.parameters.emplace(binding, p).second)
                fail(id, "out", "duplicate_parameter",
                     "Parameter key must have one declaration; share its output connections");
            result_.surface.labels[binding] = node.at("data").value("label", binding);
            expression = "g_SurfaceParameter_" + binding;
        } else if (key.starts_with("texture")) {
            const auto binding = identifier(node);
            result_.surface.labels[binding] = node.at("data").value("label", binding);
            MaterialTextureSlot slot;
            const auto semantic = node.at("data").value("semantic", std::string("color"));
            require(semantic == "color" || semantic == "data" || semantic == "normal",
                    "Unknown texture semantic");
            slot.semantic = semantic == "color"    ? TextureSemantic::Color
                            : semantic == "normal" ? TextureSemantic::Normal
                                                   : TextureSemantic::Data;
            slot.dimension = key == "texturecube"         ? TextureDimension::Cube
                             : key == "texturecube_array" ? TextureDimension::CubeArray
                             : key == "texture3d"         ? TextureDimension::D3
                             : key == "texture2d_array"   ? TextureDimension::D2Array
                                                          : TextureDimension::D2;
            if (slot.dimension == TextureDimension::D2 ||
                slot.dimension == TextureDimension::D2Array)
                slot.uv_set = uv_index(node.at("data"));
            if (!result_.surface.textures.emplace(binding, slot).second)
                fail(id, "out", "duplicate_texture",
                     "Texture slot key must have one declaration; share its output");
            if (slot.dimension == TextureDimension::D2)
                expression = "ForgeSample_" + binding + "(" +
                             (edges_.contains({id, "uv"}) ? arg(0) : "input") + ")";
            else if (slot.dimension == TextureDimension::D2Array)
                expression = "ForgeSample_" + binding + "(" +
                             (edges_.contains({id, "uv"}) ? "float3(" + arg(0) + "," + arg(1) + ")"
                                                          : "input," + arg(1)) +
                             ")";
            else
                expression = "ForgeSample_" + binding + "(" +
                             arg(0, slot.dimension == TextureDimension::Cube ? "input.Normal"
                                    : slot.dimension == TextureDimension::CubeArray
                                        ? "float4(input.Normal,0)"
                                        : "float3(0,0,0)") +
                             ")";
        } else if (key == "normal_map") {
            const auto sample = arg(0, "float4(0.5,0.5,1,1)");
            const auto uv = arg(1, "input.UV[0]");
            const auto scale = arg(2, "1.0");
            expression = "ForgeGraphNormal(input," + sample + ".xyz," + uv + "," + scale + ")";
        } else if (key == "uv") {
            const auto uv = uv_index(node.at("data"));
            result_.surface.uv_sets.push_back(uv);
            expression = "FORGE_GRAPH_UV_" + std::to_string(uv) + "_END";
        } else if (key == "world_normal")
            expression = "input.Normal";
        else if (key == "world_position")
            expression = "input.World";
        else if (key == "vertex_color")
            expression = "input.Color";
        else if (key == "combine") {
            expression = hlsl_type(t) + "(";
            for (size_t i = 0; i < inputs.size(); ++i) {
                if (i)
                    expression += ",";
                expression += arg(i, i == 3 ? "1.0" : "0.0");
            }
            expression += ")";
        } else if (key == "component") {
            const auto& lane = node.at("data").at("lane");
            require(lane.is_number_integer() && lane.get<int64_t>() >= 0 &&
                        lane.get<uint64_t>() < material_parameter_width(inputs[0].type),
                    "Component lane is outside the input type");
            expression = arg(0) + "." + std::string(1, "xyzw"[lane.get<unsigned>()]);
        } else if (key == "dot3" || key == "cross3") {
            expression = (key == "dot3" ? "dot(" : "cross(") + arg(0) + "," + arg(1) + ")";
        } else if (key == "normalize3")
            expression = "ForgeUnit(" + arg(0) + ")";
        else if (key == "add" || key == "subtract" || key == "multiply" || key == "minimum" ||
                 key == "maximum" || key == "divide") {
            const std::string a = arg(0),
                              b = arg(1, key == "multiply" ? "((" + hlsl_type(t) + ")1.0)"
                                                           : "((" + hlsl_type(t) + ")0.0)");
            if (key == "minimum" || key == "maximum")
                expression = (key == "minimum" ? "min(" : "max(") + a + "," + b + ")";
            else if (key == "divide") {
                expression = hlsl_type(t) + "(";
                for (unsigned lane = 0; lane < material_parameter_width(t); ++lane) {
                    if (lane)
                        expression += ",";
                    const auto suffix = material_parameter_width(t) == 1
                                            ? std::string{}
                                            : "." + std::string(1, "xyzw"[lane]);
                    const auto x = a + suffix, y = b + suffix, zero = "(" + y + "==0)";
                    // Select zero rather than multiply by a mask: an overflowed
                    // upstream value times zero is NaN, not the promised zero.
                    expression += "(" + zero + "?0:(" + x + "/(" + zero + "?1:" + y + ")))";
                }
                expression += ")";
            } else
                expression = "(" + a +
                             (key == "add"        ? "+"
                              : key == "subtract" ? "-"
                                                  : "*") +
                             b + ")";
        } else if (key == "lerp") {
            const auto a = arg(0), b = arg(1), factor = arg(2, "0.5");
            expression = "lerp(" + a + "," + b + "," + factor + ")";
        } else if (key == "saturate" || key == "sine" || key == "cosine" || key == "absolute" ||
                   key == "fraction" || key == "floor") {
            const auto function = key == "sine"       ? "sin"
                                  : key == "cosine"   ? "cos"
                                  : key == "absolute" ? "abs"
                                  : key == "fraction" ? "frac"
                                  : key == "floor"    ? "floor"
                                                      : "saturate";
            expression = std::string(function) + "(" + arg(0) + ")";
        } else if (key == "one_minus")
            expression = "(1-" + arg(0) + ")";
        else if (key == "convert") {
            const auto from = node.at("data").value("from", std::string("vector4"));
            require(types.contains(from), "Unknown conversion source type");
            const auto a = arg(0);
            const auto n = material_parameter_width(t),
                       m = material_parameter_width(types.at(from));
            if (n == m)
                expression = a;
            else if (m == 1)
                expression = "((" + hlsl_type(t) + ")(" + a + "))";
            else if (n < m)
                expression = a + "." + std::string("xyzw").substr(0, n);
            else {
                expression = hlsl_type(t) + "(" + a;
                for (unsigned i = m; i < n; ++i)
                    expression += i == 3 ? ",1.0" : ",0.0";
                expression += ")";
            }
        } else
            fail(id, "", "unavailable_schema", "No material compiler for this node");
        const auto name = "forge_node_" + std::to_string(temporary_++);
        body_ += "#line 1 \"graph/" +
                 (node.contains("_compile_origin")
                      ? node.at("_compile_origin").at("node").get<GraphNodeId>().str()
                      : id.str()) +
                 "/out\"\n" + hlsl_type(t) + " " + name + "=" + expression + ";\n";
        visiting_.erase(id);
        return ready_.emplace(id, Expression{t, name}).first->second;
    }

  public:
    explicit Compiler(const Json& graph) : graph_(graph) {
        for (const auto& node : graph.at("nodes"))
            nodes_.emplace(node.at("id").get<GraphNodeId>(), &node);
        for (const auto& edge : graph.at("edges")) {
            auto id = edge.at("to").at("node").get<GraphNodeId>();
            auto p = edge.at("to").at("port").get<std::string>();
            if (!edges_.emplace(std::pair{id, p}, &edge).second)
                fail(id, p, "multiple_inputs", "An input accepts one connection");
        }
    }
    CompiledMaterialGraph run() {
        const Json* output = nullptr;
        for (const auto& node : graph_.at("nodes")) {
            const auto& schemas = material_graph_node_schemas();
            const auto found = std::find_if(schemas.begin(), schemas.end(),
                                            [&](const auto& s) { return s.key == kind(node); });
            if (found == schemas.end() || node.at("version") != found->version)
                fail(node.at("id").get<GraphNodeId>(), "", "unavailable_schema",
                     "Node type/version is unavailable; source is preserved");
        }
        for (const auto& node : graph_.at("nodes"))
            if (kind(node) == "surface") {
                if (output)
                    fail(node.at("id").get<GraphNodeId>(), "", "multiple_surfaces",
                         "Use exactly one Surface Output");
                output = &node;
            }
        if (!output)
            throw std::runtime_error("Material graph requires one Surface Output");
        const auto id = output->at("id").get<GraphNodeId>();
        const std::array<std::string, 6> defaults{"float4(0.8,0.8,0.8,1)", "0.0",           "0.5",
                                                  "input.Normal",          "float3(0,0,0)", "1.0"};
        std::vector<std::string> expressions;
        for (size_t i = 0; i < surface_ports.size(); ++i)
            expressions.push_back(input(id, surface_ports[i], defaults[i]).value);
        // Validate disconnected authored nodes too: hidden schema/type/cycle errors
        // cannot silently disappear merely because an output is currently unused.
        for (const auto& [node, n] : nodes_)
            if (kind(*n) != "surface")
                (void)expression(node);
        for (const auto& [target, e] : edges_) {
            (void)e;
            const auto ports = material_graph_inputs(*nodes_.at(target.first));
            (void)find_port(ports, target.second, target.first);
        }
        for (const auto& [key, slot] : result_.surface.textures) {
            (void)key;
            result_.surface.uv_sets.push_back(slot.uv_set);
        }
        auto& uv = result_.surface.uv_sets;
        uv.push_back(0);
        std::sort(uv.begin(), uv.end());
        uv.erase(std::unique(uv.begin(), uv.end()), uv.end());
        // All graph texture helpers index a dense array, not the logical UV number.
        // Preserve logical-to-dense semantics in the actual mesh adapter.
        for (const auto i : uv) {
            const auto at = std::find(uv.begin(), uv.end(), i);
            if (at == uv.end())
                continue;
            const auto old = "FORGE_GRAPH_UV_" + std::to_string(i) + "_END";
            const auto replacement = "input.UV[" + std::to_string(at - uv.begin()) + "]";
            if (old != replacement) {
                size_t pos = 0;
                while ((pos = body_.find(old, pos)) != std::string::npos) {
                    body_.replace(pos, old.size(), replacement);
                    pos += replacement.size();
                }
            }
        }
        result_.source = "#include \"ForgeSurface.fxh\"\nfloat3 ForgeGraphNormal(ForgeSurfaceInput "
                         "input,float3 texel,float2 uv,float strength) {\n"
                         "float3 n=ForgeUnit(input.Normal);ForgeSurfaceFrame "
                         "frame=ForgePixelFrame(n,input.Tangent,input.Bitangent,ddx(input.World),"
                         "ddy(input.World),ddx(uv),ddy(uv));\n"
                         "float3 mapped=texel*2-1;mapped.xy*=strength;return "
                         "ForgePerturbNormal(frame,mapped); }\n";
        result_.source += "struct ForgeGraphValues {float4 BaseColor;float Metallic;float "
                          "Roughness;float3 Normal;float3 Emissive;float Occlusion;};\n"
                          "ForgeGraphValues ForgeGraphSurface(ForgeSurfaceInput input) {\n" +
                          body_ + "ForgeGraphValues result=(ForgeGraphValues)0;\n";
        const std::array fields{"BaseColor", "Metallic", "Roughness",
                                "Normal",    "Emissive", "Occlusion"};
        for (size_t i = 0; i < fields.size(); ++i)
            result_.source += "result." + std::string(fields[i]) + "=" + expressions[i] + ";\n";
        result_.source += "return result; }\n";
        result_.surface.physically_based = true;
        validate_surface_definition(result_.surface);
        return std::move(result_);
    }
};
} // namespace
MaterialGraphError::MaterialGraphError(MaterialGraphDiagnostic d)
    : std::runtime_error("Material graph [" + d.code + "] node " +
                         (d.node ? d.node.str() : std::string("none")) + " port " + d.port + ": " +
                         d.message),
      diagnostic_(std::move(d)) {}
const std::vector<MaterialGraphNodeSchema>& material_graph_node_schemas() {
    static const std::vector<MaterialGraphNodeSchema> schemas{
        {"surface", "Surface Output", "Output",
         "Lit surface color, metalness, roughness, world normal, emission and occlusion."},
        {"constant", "Constant", "Values", "A typed scalar, vector or linear-color constant."},
        {"parameter", "Parameter", "Values",
         "Stable material binding key with an editable instance value."},
        {"texture2d", "Texture 2D", "Textures",
         "Sample a texture slot assigned by the material; select color/data/normal usage."},
        {"texture2d_array", "Texture 2D Array", "Textures",
         "Sample a planar array texture at UV coordinates and a layer."},
        {"texturecube", "Texture Cube", "Textures", "Sample a cube texture with a direction."},
        {"texturecube_array", "Texture Cube Array", "Textures",
         "Sample a cube array with direction and array index."},
        {"texture3d", "Texture 3D", "Textures", "Sample a volume texture with three coordinates."},
        {"combine", "Combine", "Values",
         "Assemble scalar lanes into a typed vector or linear color."},
        {"component", "Component", "Values",
         "Read one scalar lane from a vector or color, including RGB and alpha channels."},
        {"dot3", "Dot Product", "Math", "Dot product of two Vector3 inputs."},
        {"cross3", "Cross Product", "Math", "Cross product of two Vector3 inputs."},
        {"normalize3", "Normalize", "Math", "Safe Vector3 normalization; zero remains zero."},
        {"minimum", "Minimum", "Math", "Component-wise minimum of matching inputs."},
        {"maximum", "Maximum", "Math", "Component-wise maximum of matching inputs."},
        {"divide", "Divide", "Math", "Component-wise division; a zero denominator returns zero."},
        {"cosine", "Cosine", "Math", "Component-wise cosine in radians."},
        {"absolute", "Absolute", "Math", "Component-wise absolute value."},
        {"fraction", "Fraction", "Math", "Fractional part of each lane."},
        {"floor", "Floor", "Math", "Round each lane down to an integer."},
        {"normal_map", "Normal Map", "Textures",
         "Decode a sampled tangent-space normal into the renderer's world-space surface frame."},
        {"uv", "UV Coordinates", "Geometry", "A logical mesh UV set, mapped by the renderer."},
        {"world_normal", "World Normal", "Geometry", "Interpolated world-space surface normal."},
        {"world_position", "Camera-relative Position", "Geometry",
         "Camera-relative world position, matching the renderer's precision contract."},
        {"vertex_color", "Vertex Color", "Geometry", "Linear vertex color."},
        {"add", "Add", "Math", "Add two values of the same type."},
        {"subtract", "Subtract", "Math", "Subtract two values of the same type."},
        {"multiply", "Multiply", "Math",
         "Multiply two values of the same type; Convert broadcasts a scalar."},
        {"lerp", "Mix", "Math", "Interpolate matching values using a scalar factor."},
        {"saturate", "Clamp 0–1", "Math", "Clamp each component to zero through one."},
        {"sine", "Sine", "Math", "Component-wise sine, in radians."},
        {"one_minus", "One Minus", "Math", "Subtract each component from one."},
        {"function", "Function", "Functions", "An instance of a reusable pure material subgraph."},
        {"function_input", "Function Input", "Functions",
         "Stable typed function input; created during extraction."},
        {"function_output", "Function Output", "Functions",
         "The single typed value returned by this function."},
        {"convert", "Convert", "Values",
         "Explicit vector/color conversion, scalar broadcast or component truncation."}};
    return schemas;
}
Json create_material_graph_node(std::string_view schema, std::array<double, 2> position) {
    const auto& schemas = material_graph_node_schemas();
    const auto found = std::find_if(schemas.begin(), schemas.end(),
                                    [&](const auto& s) { return s.key == schema; });
    require(found != schemas.end(), "Unknown node schema");
    Json node{{"id", GraphNodeId::generate()},
              {"type", schema},
              {"version", 1},
              {"position", position},
              {"data", Json::object()}};
    auto& d = node["data"];
    if (schema == "constant" || schema == "parameter")
        d = {{"type", "color4"}, {"value", {0.8, 0.8, 0.8, 1.0}}};
    else
        d["type"] = "scalar";
    if (schema == "parameter" || schema.starts_with("texture")) {
        auto key = node.at("id").get<GraphNodeId>().str();
        key.erase(std::remove(key.begin(), key.end(), '-'), key.end());
        d["key"] = "p_" + key;
        d["label"] = schema == "parameter" ? "Color" : "Texture";
    }
    if (schema == "function") {
        d["inputs"] = Json::array();
        d["type"] = "scalar";
    }
    if (schema == "function_input") {
        d["key"] = "input";
        d["label"] = "Input";
    }
    if (schema.starts_with("texture")) {
        d["semantic"] = "color";
        d["uv_set"] = 0;
    }
    if (schema == "uv")
        d["uv_set"] = 0;
    if (schema == "component") {
        d["from"] = "color4";
        d["lane"] = 0;
    }
    if (schema == "combine")
        d["type"] = "color4";
    if (schema == "convert") {
        d["from"] = "vector4";
        d["type"] = "color4";
    }
    return node;
}
Json create_material_graph() {
    auto output = create_material_graph_node("surface", {400, 60});
    auto color = create_material_graph_node("parameter", {30, 60});
    Json connection{{"id", GraphEdgeId::generate()},
                    {"from", {{"node", color.at("id")}, {"port", "out"}}},
                    {"to", {{"node", output.at("id")}, {"port", "base_color"}}}};
    return {{"format", "forge.material.graph"},
            {"version", 1},
            {"nodes", Json::array({color, output})},
            {"edges", Json::array({connection})}};
}
void validate_material_graph_document(const Json& graph) {
    persistable(graph);
    require(graph.is_object() && graph.at("format") == "forge.material.graph" &&
                graph.at("version").is_number_integer() && graph.at("version") == 1,
            "Unsupported graph document");
    require(graph.dump().size() <= 1024 * 1024, "Graph exceeds document byte limit");
    require(graph.at("nodes").is_array() && graph.at("nodes").size() <= 1024 &&
                graph.at("edges").is_array() && graph.at("edges").size() <= 4096,
            "Graph node/edge limit exceeded");
    if (graph.contains("functions")) {
        require(graph.at("functions").is_array() && graph.at("functions").size() <= 64,
                "Function count limit");
        std::set<GraphFunctionId> ids;
        for (const auto& f : graph.at("functions")) {
            require(ids.insert(f.at("id").get<GraphFunctionId>()).second &&
                        f.at("label").is_string() && f.at("label").get<std::string>().size() <= 512,
                    "Invalid function identity/label");
            require(!f.at("graph").contains("functions"),
                    "Functions belong to the root graph owner");
            validate_material_graph_document(f.at("graph"));
        }
    }
    std::set<GraphNodeId> nodes;
    for (const auto& n : graph.at("nodes")) {
        require(n.is_object() && nodes.insert(n.at("id").get<GraphNodeId>()).second,
                "Duplicate/invalid node identity");
        require(n.at("type").is_string() && n.at("type").get<std::string>().size() <= 128 &&
                    n.at("version").is_number_integer() && n.at("version").get<int64_t>() > 0 &&
                    n.at("version").get<int64_t>() <= 65535,
                "Invalid node schema identity/version");
        require(n.at("data").is_object() && n.at("position").is_array() &&
                    n.at("position").size() == 2,
                "Invalid node data/position");
        for (const auto& p : n.at("position"))
            require(p.is_number() && std::isfinite(p.get<double>()) &&
                        std::abs(p.get<double>()) <= 1000000,
                    "Invalid graph position");
    }
    std::set<GraphEdgeId> edges;
    for (const auto& e : graph.at("edges")) {
        require(edges.insert(e.at("id").get<GraphEdgeId>()).second, "Duplicate edge identity");
        for (const auto* endpoint : {"from", "to"}) {
            const auto& p = e.at(endpoint);
            require(nodes.contains(p.at("node").get<GraphNodeId>()),
                    "Connection targets absent node");
            const auto key = p.at("port").get<std::string>();
            require(!key.empty() && key.size() <= 128, "Invalid port identity");
        }
    }
}
std::vector<MaterialGraphPort> material_graph_inputs(const Json& node) {
    const auto k = kind(node);
    if (k == "surface")
        return surface_ports;
    if (k == "function") {
        std::vector<MaterialGraphPort> ports;
        require(node.at("data").at("inputs").is_array() &&
                    node.at("data").at("inputs").size() <= 64,
                "Function input limit");
        std::set<std::string> keys;
        for (const auto& p : node.at("data").at("inputs")) {
            const auto key = p.at("key").get<std::string>();
            const auto t = p.at("type").get<std::string>();
            require(!key.empty() && key.size() <= 128 && keys.insert(key).second &&
                        types.contains(t),
                    "Invalid function port");
            ports.push_back({key, p.value("label", key), types.at(t)});
        }
        return ports;
    }
    if (k == "function_output")
        return {port("value", type(node))};
    if (k == "texture2d")
        return {port("uv", Type::Vector2)};
    if (k == "texture2d_array")
        return {port("uv", Type::Vector2), port("layer", Type::Scalar)};
    if (k == "texturecube" || k == "texture3d")
        return {port("coordinate", Type::Vector3)};
    if (k == "texturecube_array")
        return {port("coordinate", Type::Vector4)};
    if (k == "combine") {
        const auto t = type(node);
        std::vector<MaterialGraphPort> ports;
        require(t != Type::Scalar, "Combine needs a vector or color type");
        for (unsigned i = 0; i < material_parameter_width(t); ++i)
            ports.push_back(port(std::string(1, "xyzw"[i]), Type::Scalar));
        return ports;
    }
    if (k == "dot3" || k == "cross3")
        return {port("a", Type::Vector3), port("b", Type::Vector3)};
    if (k == "normalize3")
        return {port("value", Type::Vector3)};
    if (k == "normal_map")
        return {port("sample", Type::Vector4), port("uv", Type::Vector2),
                port("strength", Type::Scalar)};
    if (k == "add" || k == "subtract" || k == "multiply" || k == "minimum" || k == "maximum" ||
        k == "divide")
        return {port("a", type(node)), port("b", type(node))};
    if (k == "lerp")
        return {port("a", type(node)), port("b", type(node)), port("factor", Type::Scalar)};
    if (k == "saturate" || k == "sine" || k == "one_minus" || k == "cosine" || k == "absolute" ||
        k == "fraction" || k == "floor")
        return {port("value", type(node))};
    if (k == "convert" || k == "component") {
        const auto key = node.at("data").value("from", std::string("vector4"));
        require(types.contains(key), "Unknown conversion input type");
        return {port("value", types.at(key))};
    }
    return {};
}
std::vector<MaterialGraphPort> material_graph_outputs(const Json& node) {
    const auto k = kind(node);
    if (k == "surface" || k == "function_output")
        return {};
    if (k.starts_with("texture"))
        return {port("out", node.at("data").value("semantic", std::string("color")) == "color"
                                ? Type::LinearColor4
                                : Type::Vector4)};
    if (k == "uv")
        return {port("out", Type::Vector2)};
    if (k == "component" || k == "dot3")
        return {port("out", Type::Scalar)};
    if (k == "cross3" || k == "normalize3" || k == "normal_map" || k == "world_normal" ||
        k == "world_position")
        return {port("out", Type::Vector3)};
    if (k == "vertex_color")
        return {port("out", Type::LinearColor4)};
    return {port("out", type(node))};
}
CompiledMaterialGraph compile_material_graph(const Json& graph) {
    validate_material_graph_document(graph);
    const auto expanded = expand_material_graph_functions(graph);
    try {
        return Compiler(expanded).run();
    } catch (const MaterialGraphError& e) {
        for (const auto& n : expanded.at("nodes"))
            if (n.at("id").get<GraphNodeId>() == e.diagnostic().node &&
                n.contains("_compile_origin")) {
                auto d = e.diagnostic();
                d.node = n.at("_compile_origin").at("node").get<GraphNodeId>();
                d.message +=
                    " (function " + n.at("_compile_origin").at("function").get<std::string>() + ")";
                throw MaterialGraphError(std::move(d));
            }
        throw;
    }
}
Json copy_material_graph_selection(const Json& graph, const std::set<GraphNodeId>& selected) {
    validate_material_graph_document(graph);
    Json result{{"nodes", Json::array()}, {"edges", Json::array()}};
    if (graph.contains("functions"))
        result["functions"] = graph.at("functions");
    for (const auto& n : graph.at("nodes"))
        if (selected.contains(n.at("id").get<GraphNodeId>()))
            result["nodes"].push_back(n);
    for (const auto& e : graph.at("edges"))
        if (selected.contains(e.at("from").at("node").get<GraphNodeId>()) &&
            selected.contains(e.at("to").at("node").get<GraphNodeId>()))
            result["edges"].push_back(e);
    return result;
}
void paste_material_graph_selection(Json& graph, const Json& clipboard,
                                    std::array<double, 2> offset) {
    Json next = graph;
    if (clipboard.contains("functions")) {
        if (!next.contains("functions"))
            next["functions"] = Json::array();
        for (const auto& f : clipboard.at("functions")) {
            const auto same =
                std::find_if(next["functions"].begin(), next["functions"].end(),
                             [&](const auto& old) { return old.at("id") == f.at("id"); });
            if (same == next["functions"].end())
                next["functions"].push_back(f);
            else
                require(*same == f,
                        "Clipboard function identity conflicts with a different definition");
        }
    }
    std::map<GraphNodeId, GraphNodeId> ids;
    require(clipboard.at("nodes").is_array() && clipboard.at("nodes").size() <= 1024 &&
                clipboard.at("edges").is_array() && clipboard.at("edges").size() <= 4096,
            "Invalid graph clipboard");
    for (auto n : clipboard.at("nodes")) {
        const auto old = n.at("id").get<GraphNodeId>();
        require(!ids.contains(old), "Duplicate clipboard node");
        n["id"] = ids.emplace(old, GraphNodeId::generate()).first->second;
        for (size_t i = 0; i < 2; ++i)
            n["position"][i] = n.at("position").at(i).get<double>() + offset[i];
        // Duplicating a parameter/texture declaration needs a new binding identity.
        // Only the known field is changed; all opaque payload remains intact.
        if (kind(n) == "parameter" || kind(n).starts_with("texture")) {
            auto key = n.at("id").get<GraphNodeId>().str();
            key.erase(std::remove(key.begin(), key.end(), '-'), key.end());
            n["data"]["key"] = "p_" + key;
        }
        next["nodes"].push_back(std::move(n));
    }
    for (auto e : clipboard.at("edges")) {
        e["id"] = GraphEdgeId::generate();
        for (const auto* endpoint : {"from", "to"})
            e[endpoint]["node"] = ids.at(e.at(endpoint).at("node").get<GraphNodeId>());
        next["edges"].push_back(std::move(e));
    }
    validate_material_graph_document(next);
    graph = std::move(next);
}
AssetId MaterialGraphSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void MaterialGraphSource::validate() const {
    persistable(document);
    require(document.is_object() && document.at("format") == "forge.shader" &&
                document.at("version").is_number_integer() && document.at("version") == 3,
            "Unsupported graph shader source");
    (void)asset();
    require(!document.contains("stages") && !document.contains("surface") &&
                !document.contains("source_root"),
            "Graph source must not store a second authored HLSL/interface authority");
    require(document.dump().size() <= 1024 * 1024, "Graph shader source exceeds byte limit");
    validate_material_graph_document(document.at("graph"));
}
MaterialGraphSource MaterialGraphSource::duplicate(AssetId id) const {
    validate();
    require(id && id != asset(), "A duplicated Shader requires a new AssetId");
    auto result = *this;
    result.document["asset_id"] = id;
    auto& root = result.document["graph"];
    std::map<GraphFunctionId, GraphFunctionId> functions;
    if (root.contains("functions"))
        for (auto& f : root["functions"]) {
            const auto old = f.at("id").get<GraphFunctionId>();
            f["id"] = functions.emplace(old, GraphFunctionId::generate()).first->second;
        }
    const auto remap_graph = [&](Json& graph) {
        std::map<GraphNodeId, GraphNodeId> nodes;
        for (auto& n : graph["nodes"]) {
            const auto old = n.at("id").get<GraphNodeId>();
            n["id"] = nodes.emplace(old, GraphNodeId::generate()).first->second;
            if (n.at("type") == "function" && n.at("version") == 1) {
                const auto fn = n.at("data").at("function").get<GraphFunctionId>();
                if (functions.contains(fn))
                    n["data"]["function"] = functions.at(fn);
            }
        }
        for (auto& e : graph["edges"]) {
            e["id"] = GraphEdgeId::generate();
            for (const auto* endpoint : {"from", "to"})
                e[endpoint]["node"] = nodes.at(e.at(endpoint).at("node").get<GraphNodeId>());
        }
    };
    remap_graph(root);
    if (root.contains("functions"))
        for (auto& f : root["functions"])
            remap_graph(f["graph"]);
    result.validate();
    return result;
}
MaterialGraphSource MaterialGraphSource::parse(std::span<const std::byte> bytes) {
    MaterialGraphSource result{asset_detail::parse_bounded_json(bytes, 1024 * 1024)};
    result.validate();
    return result;
}
MaterialGraphSource MaterialGraphSource::create(AssetId id) {
    MaterialGraphSource result{{{"format", "forge.shader"},
                                {"version", 3},
                                {"asset_id", id},
                                {"graph", create_material_graph()}}};
    result.validate();
    return result;
}
} // namespace forge
