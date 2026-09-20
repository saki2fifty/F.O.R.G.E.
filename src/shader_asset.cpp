#include "asset_bytes.hpp"
#include "cooked_envelope.hpp"
#include <algorithm>
#include <forge/shader_asset.hpp>
#include <tuple>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t source_file_limit = 2 * 1024 * 1024, source_limit = 16 * 1024 * 1024;
constexpr std::size_t bytecode_limit = 16 * 1024 * 1024, metadata_limit = 2 * 1024 * 1024;
constexpr std::array<std::byte, 8> magic{std::byte{'F'}, std::byte{'R'}, std::byte{'G'},
                                         std::byte{'S'}, std::byte{'H'}, std::byte{'D'},
                                         std::byte{0},   std::byte{0}};
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void identifier(std::string_view name) {
    require(!name.empty() && name.size() <= 255, "Invalid shader identifier length");
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = name[i];
        require((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
                    (i && c >= '0' && c <= '9'),
                "Invalid shader identifier");
    }
}
void filename(std::string_view name) {
    require(!name.empty() && name.size() <= 1024 && name.find('\0') == name.npos &&
                name.find('\\') == name.npos && name.find(':') == name.npos,
            "Invalid shader virtual filename");
    const auto p = ProjectPaths::normalize(std::filesystem::u8path(name));
    require(path_utf8(p) == name, "Shader virtual filename must be canonical");
    for (const unsigned char c : name)
        require(c >= 32 && c != 127, "Shader virtual filename contains control characters");
}
void macro_value(std::string_view value) {
    require(value.size() <= 255, "Shader define exceeds length limit");
    for (const unsigned char c : value)
        require(c >= 32 && c < 127 && c != '#',
                "Shader define must be a single ASCII preprocessing value");
}
std::uint32_t count(const Json& v, std::uint32_t maximum) {
    require(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>() >= 0),
            "Shader count/offset must be an unsigned integer");
    const auto n = v.get<std::uint64_t>();
    require(n <= maximum, "Shader count/offset exceeds profile");
    return static_cast<std::uint32_t>(n);
}
void stage_set(const std::set<ShaderStage>& stages) {
    require(!stages.empty() && stages.size() <= 6, "Invalid shader stage count");
    if (stages.contains(ShaderStage::Compute))
        require(stages.size() == 1, "Compute and graphics stages need separate shader assets");
    else {
        require(stages.contains(ShaderStage::Vertex), "Graphics shader requires a vertex stage");
        require(stages.contains(ShaderStage::Hull) == stages.contains(ShaderStage::Domain),
                "Tessellation requires both hull and domain stages");
    }
}
void variables(const Json& fields, std::uint32_t parent_size, unsigned depth, std::size_t& total) {
    require(depth <= 8 && fields.is_array() && fields.size() <= 4096,
            "Shader reflection nesting/field count exceeds profile");
    std::set<std::string> names;
    for (const auto& field : fields) {
        require(++total <= 4096 && field.is_object(), "Shader reflection field budget exceeded");
        const auto name = field.at("name").get<std::string>();
        identifier(name);
        require(names.insert(name).second, "Duplicate reflected shader field");
        const auto offset = count(field.at("offset"), 65536);
        require(offset < parent_size, "Shader field lies outside its parent buffer");
        const auto cls = field.at("class").get<std::string>();
        const auto basic = field.at("basic").get<std::string>();
        static const std::set<std::string> classes{"scalar", "vector", "matrix-rows",
                                                   "matrix-columns", "struct"};
        static const std::set<std::string> types{
            "unknown",   "void",       "bool",       "int",      "int8",     "int16",    "int64",
            "uint",      "uint8",      "uint16",     "uint64",   "float",    "float16",  "double",
            "min8float", "min10float", "min16float", "min12int", "min16int", "min16uint"};
        require(classes.contains(cls) && types.contains(basic),
                "Unsupported reflected shader type");
        const auto rows = count(field.at("rows"), 4), columns = count(field.at("columns"), 4);
        (void)count(field.at("array_size"), 65536);
        const auto& children = field.at("members");
        if (cls == "struct") {
            require(children.is_array() && !children.empty(), "Reflected shader struct is empty");
            variables(children, parent_size - offset, depth + 1, total);
        } else {
            require(children.is_array() && children.empty() && rows && columns &&
                        basic != "unknown" && basic != "void",
                    "Invalid reflected scalar/vector/matrix shape");
            require(cls != "scalar" || (rows == 1 && columns == 1), "Invalid shader scalar shape");
            require(cls != "vector" || rows == 1, "Invalid shader vector shape");
        }
    }
}
Json layout(const ShaderData& data) {
    Json stages = Json::object();
    for (const auto& stage : data.stages)
        stages[shader_stage_name(stage.stage)] = stage.reflection;
    return {{"backend", "d3d12"},
            {"profile", "5.1"},
            {"row_major", data.row_major},
            {"stages", stages}};
}
} // namespace
const char* shader_stage_name(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return "vertex";
    case ShaderStage::Pixel:
        return "pixel";
    case ShaderStage::Compute:
        return "compute";
    case ShaderStage::Geometry:
        return "geometry";
    case ShaderStage::Hull:
        return "hull";
    case ShaderStage::Domain:
        return "domain";
    }
    throw std::runtime_error("Unsupported shader stage");
}
ShaderStage shader_stage(std::string_view name) {
    for (auto stage : {ShaderStage::Vertex, ShaderStage::Pixel, ShaderStage::Compute,
                       ShaderStage::Geometry, ShaderStage::Hull, ShaderStage::Domain})
        if (name == shader_stage_name(stage))
            return stage;
    throw std::runtime_error("Shader stage is unavailable in the D3D12/FXC5.1 profile");
}
void validate_shader_program(const ShaderProgramSource& program) {
    require(program.stages.size() <= 6 && program.defines.size() <= 64 &&
                program.permutations.size() <= 16 && program.optimization <= 3,
            "Shader program exceeds compiler profile");
    std::set<ShaderStage> stages;
    for (const auto& entry : program.stages) {
        (void)shader_stage_name(entry.stage);
        require(stages.insert(entry.stage).second, "Duplicate shader stage");
        filename(entry.source);
        identifier(entry.entry);
    }
    stage_set(stages);
    for (const auto& [name, value] : program.defines) {
        identifier(name);
        macro_value(value);
    }
    for (const auto& [name, choices] : program.permutations) {
        identifier(name);
        require(!program.defines.contains(name) && !choices.empty() && choices.size() <= 32,
                "Invalid shader permutation choices or fixed-define conflict");
        std::set<std::string> values;
        for (const auto& choice : choices) {
            macro_value(choice);
            require(values.insert(choice).second, "Duplicate shader permutation value");
        }
    }
}
ShaderProgramSource shader_program_source(const Json& doc) {
    require(doc.dump().size() <= 1024 * 1024, "Shader source document exceeds byte profile");
    require(doc.is_object() && doc.at("format") == "forge.shader" && doc.at("version") == 1,
            "Unsupported shader source document");
    (void)doc.at("asset_id").get<AssetId>();
    ShaderProgramSource result;
    require(doc.at("stages").is_array() && doc.at("stages").size() <= 6,
            "Invalid shader source stage list");
    for (const auto& entry : doc.at("stages"))
        result.stages.push_back({shader_stage(entry.at("stage").get<std::string>()),
                                 entry.at("source"), entry.value("entry", std::string("main"))});
    result.defines = doc.value("defines", std::map<std::string, std::string>{});
    result.permutations =
        doc.value("permutations", std::map<std::string, std::vector<std::string>>{});
    result.row_major = doc.value("row_major", true);
    result.optimization = count(doc.value("optimization", Json(2)), 3);
    validate_shader_program(result);
    return result;
}
std::map<std::string, std::string>
select_shader_permutation(const ShaderProgramSource& program,
                          const std::map<std::string, std::string>& selected) {
    validate_shader_program(program);
    require(selected.size() == program.permutations.size(),
            "Select exactly one value for each shader permutation axis");
    auto result = program.defines;
    for (const auto& [name, value] : selected) {
        const auto found = program.permutations.find(name);
        require(found != program.permutations.end() &&
                    std::find(found->second.begin(), found->second.end(), value) !=
                        found->second.end(),
                "Unknown shader permutation axis/value");
        result.emplace(name, value);
    }
    return result;
}
void validate_shader_sources(const ShaderSources& sources) {
    require(!sources.empty() && sources.size() <= 256, "Shader source file count exceeds profile");
    std::size_t total = 0;
    std::set<std::string> folded;
    for (const auto& [name, source] : sources) {
        filename(name);
        auto lower = name;
        for (auto& c : lower)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        require(folded.insert(lower).second, "Shader filenames collide under Windows case rules");
        require(source.size() <= source_file_limit && source.find('\0') == source.npos,
                "Shader source has embedded NUL or exceeds file limit");
        total += source.size();
        require(total <= source_limit, "Shader source snapshot exceeds byte limit");
    }
}
AssetBuildInput shader_build_input(const ShaderProgramSource& program, const ShaderSources& sources,
                                   const std::map<std::string, std::string>& permutation,
                                   std::string compiler, bool debug) {
    const auto defines = select_shader_permutation(program, permutation);
    validate_shader_sources(sources);
    require(valid_content_digest(compiler), "Shader compiler digest is missing/invalid");
    Json entries = Json::object();
    for (const auto& entry : program.stages) {
        require(sources.contains(entry.source), "Shader entry source is absent from snapshot");
        entries[shader_stage_name(entry.stage)] = {{"source", entry.source},
                                                   {"entry", entry.entry}};
    }
    AssetBuildInput input;
    input.source_digest = asset_build_digest(entries);
    input.importer = "forge.shader.diligent";
    input.importer_revision = "1-core744f079f61cdbda15d371383682418fc927e4a61";
    input.settings = {{"defines", defines},
                      {"row_major", program.row_major},
                      {"optimization", program.optimization},
                      {"compiler_debug", debug}};
    input.output_format = "forge.shader.dxbc";
    input.platform = "windows-x64";
    input.backend = "d3d12";
    input.profile = "fxc-5.1";
    input.tool_revisions["d3dcompiler_47"] = std::move(compiler);
    for (const auto& [name, source] : sources)
        input.source_dependencies[name] =
            asset_detail::content_digest(std::as_bytes(std::span(source)));
    (void)input.key();
    return input;
}
void validate_shader_reflection(const Json& reflection) {
    require(reflection.is_object() && reflection.at("resources").is_array() &&
                reflection.at("resources").size() <= 256,
            "Invalid shader resource reflection");
    const auto stage = shader_stage(reflection.at("stage").get<std::string>());
    const auto& threads = reflection.at("threads");
    require(threads.is_array() && threads.size() == 3, "Invalid shader thread-group reflection");
    std::uint64_t thread_count = 1;
    for (const auto& dim : threads) {
        const auto value = count(dim, 1024);
        require(stage == ShaderStage::Compute ? value > 0 : value == 0,
                "Invalid shader thread-group dimensions");
        thread_count *= value;
    }
    require(thread_count <= 1024, "Shader thread group exceeds D3D12 profile");
    std::set<std::string> names;
    std::set<std::tuple<std::string, std::uint32_t, std::uint32_t>> bindings;
    std::size_t fields = 0;
    for (const auto& resource : reflection.at("resources")) {
        const auto name = resource.at("name").get<std::string>();
        require(!name.empty() && name.size() <= 255, "Invalid reflected resource name");
        for (const auto c : name)
            require(c >= 32 && c < 127, "Invalid reflected resource name character");
        require(names.insert(name).second, "Duplicate reflected shader resource");
        const auto kind = resource.at("kind").get<std::string>();
        const auto reg = count(resource.at("register"), 65535),
                   space = count(resource.at("space"), 65535);
        const auto size = count(resource.at("array_size"), 4096);
        require(size && size <= 65536 - reg, "Unbounded/oversized shader resource array");
        static const std::set<std::string> kinds{"constant_buffer", "texture_srv", "buffer_srv",
                                                 "texture_uav",     "buffer_uav",  "sampler"};
        require(kinds.contains(kind), "Unsupported reflected shader resource kind");
        const auto dimension = resource.at("dimension").get<std::string>();
        static const std::set<std::string> dimensions{
            "unknown",         "buffer",          "bufferex",
            "texture1d",       "texture1d_array", "texture2d",
            "texture2d_array", "texture2d_ms",    "texture2d_ms_array",
            "texture3d",       "texture_cube",    "texture_cube_array"};
        require(dimensions.contains(dimension), "Unsupported reflected resource dimension");
        if (kind.starts_with("texture"))
            require(dimension.starts_with("texture"), "Texture binding dimension mismatch");
        else if (kind.starts_with("buffer"))
            require(dimension == "buffer" || dimension == "bufferex",
                    "Buffer binding dimension mismatch");
        else
            require(dimension == "unknown", "Unexpected constant/sampler resource dimension");
        const auto cls = kind == "constant_buffer" ? "b"
                         : kind == "sampler"       ? "s"
                         : kind.ends_with("_srv")  ? "t"
                                                   : "u";
        for (std::uint32_t i = 0; i < size; ++i)
            require(bindings.emplace(cls, space, reg + i).second,
                    "Overlapping shader resource registers");
        if (kind == "constant_buffer") {
            const auto bytes = count(resource.at("size"), 65536);
            require(bytes && bytes % 16 == 0, "Invalid reflected constant buffer size");
            variables(resource.at("variables"), bytes, 0, fields);
        } else
            require(!resource.contains("variables") && !resource.contains("size"),
                    "Only constant buffers carry member layouts");
    }
    require(reflection.dump().size() <= metadata_limit, "Shader reflection exceeds metadata limit");
}
void validate_shader(const ShaderData& data) {
    require(valid_content_digest(data.build_key) && valid_content_digest(data.compiler_digest),
            "Shader artifact provenance is invalid");
    require(data.stages.size() <= 6, "Shader artifact stage count exceeds profile");
    std::set<ShaderStage> stages;
    std::size_t bytes = 0;
    for (const auto& stage : data.stages) {
        (void)shader_stage_name(stage.stage);
        identifier(stage.entry);
        require(stages.insert(stage.stage).second, "Shader artifact contains a repeated stage");
        require(!stage.bytecode.empty() && stage.bytecode.size() <= bytecode_limit,
                "Shader bytecode exceeds profile");
        bytes += stage.bytecode.size();
        require(bytes <= bytecode_limit, "Shader program bytecode exceeds profile");
        validate_shader_reflection(stage.reflection);
        require(stage.reflection.at("stage") == shader_stage_name(stage.stage),
                "Shader stage/reflection mismatch");
    }
    stage_set(stages);
}
std::string ShaderData::layout_digest() const {
    validate_shader(*this);
    return asset_build_digest(layout(*this));
}
std::size_t ShaderData::resident_bytes() const {
    std::size_t size = sizeof(*this) + build_key.capacity() + compiler_digest.capacity() +
                       stages.capacity() * sizeof(ShaderStageData);
    for (const auto& stage : stages)
        size +=
            stage.entry.capacity() + stage.bytecode.capacity() + stage.reflection.dump().size() * 4;
    return size;
}
std::vector<std::byte> encode_shader(const ShaderData& data) {
    validate_shader(data);
    Json stages = Json::array();
    std::vector<std::byte> payload;
    for (const auto& stage : data.stages) {
        stages.push_back({{"stage", shader_stage_name(stage.stage)},
                          {"entry", stage.entry},
                          {"offset", payload.size()},
                          {"size", stage.bytecode.size()},
                          {"digest", asset_detail::content_digest(stage.bytecode)},
                          {"reflection", stage.reflection}});
        payload.insert(payload.end(), stage.bytecode.begin(), stage.bytecode.end());
    }
    return asset_detail::encode_envelope({{"backend", "d3d12"},
                                          {"profile", "fxc-5.1"},
                                          {"build_key", data.build_key},
                                          {"compiler_digest", data.compiler_digest},
                                          {"compiler_debug", data.compiler_debug},
                                          {"row_major", data.row_major},
                                          {"layout_digest", data.layout_digest()},
                                          {"stages", stages}},
                                         payload, magic, metadata_limit);
}
ShaderData decode_shader(std::span<const std::byte> bytes) {
    const auto envelope =
        asset_detail::decode_envelope(bytes, magic, metadata_limit, bytecode_limit);
    const auto& m = envelope.metadata;
    require(m.at("backend") == "d3d12" && m.at("profile") == "fxc-5.1",
            "Unsupported shader backend/profile");
    ShaderData data;
    data.build_key = m.at("build_key");
    data.compiler_digest = m.at("compiler_digest");
    data.row_major = m.at("row_major");
    data.compiler_debug = m.at("compiler_debug");
    require(m.at("stages").is_array() && m.at("stages").size() <= 6,
            "Invalid shader artifact stages");
    std::size_t at = 0;
    for (const auto& entry : m.at("stages")) {
        const auto offset = count(entry.at("offset"), bytecode_limit),
                   size = count(entry.at("size"), bytecode_limit);
        require(offset == at && size && size <= envelope.payload.size() - at,
                "Invalid shader bytecode range");
        const auto code = envelope.payload.subspan(at, size);
        require(entry.at("digest") == asset_detail::content_digest(code),
                "Shader bytecode digest mismatch");
        data.stages.push_back({shader_stage(entry.at("stage").get<std::string>()),
                               entry.at("entry"),
                               {code.begin(), code.end()},
                               entry.at("reflection")});
        at += size;
    }
    require(at == envelope.payload.size(), "Trailing shader bytecode bytes");
    validate_shader(data);
    require(m.at("layout_digest") == data.layout_digest(),
            "Shader reflected layout digest mismatch");
    return data;
}
} // namespace forge
