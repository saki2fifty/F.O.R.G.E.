#include "shader_diligent.hpp"
#include "Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp"
#include "Graphics/GraphicsEngineD3D12/interface/ShaderD3D12.h"
#include "Graphics/GraphicsTools/interface/ShaderSourceFactoryUtils.h"
#include "asset_bytes.hpp"
#include <algorithm>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>
namespace forge::asset_detail {
namespace {
using namespace Diligent;
using Json = nlohmann::json;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
SHADER_TYPE native_stage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return SHADER_TYPE_VERTEX;
    case ShaderStage::Pixel:
        return SHADER_TYPE_PIXEL;
    case ShaderStage::Compute:
        return SHADER_TYPE_COMPUTE;
    case ShaderStage::Geometry:
        return SHADER_TYPE_GEOMETRY;
    case ShaderStage::Hull:
        return SHADER_TYPE_HULL;
    case ShaderStage::Domain:
        return SHADER_TYPE_DOMAIN;
    }
    throw std::runtime_error("Unknown shader stage");
}
ShaderStage reflected_stage(UINT version) {
    require(D3D12_SHVER_GET_MAJOR(version) == 5 && D3D12_SHVER_GET_MINOR(version) == 1,
            "Shader bytecode does not match its5.1 profile");
    switch (D3D12_SHVER_GET_TYPE(version)) {
    case D3D12_SHVER_VERTEX_SHADER:
        return ShaderStage::Vertex;
    case D3D12_SHVER_PIXEL_SHADER:
        return ShaderStage::Pixel;
    case D3D12_SHVER_COMPUTE_SHADER:
        return ShaderStage::Compute;
    case D3D12_SHVER_GEOMETRY_SHADER:
        return ShaderStage::Geometry;
    case D3D12_SHVER_HULL_SHADER:
        return ShaderStage::Hull;
    case D3D12_SHVER_DOMAIN_SHADER:
        return ShaderStage::Domain;
    default:
        throw std::runtime_error("Shader bytecode stage is outside selected profile");
    }
}
void device_profile(IRenderDevice* device, ShaderStage stage) {
    require(device && device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D12,
            "Shader profile requires a D3D12 device");
    const auto& info = device->GetDeviceInfo();
    require(info.MaxShaderVersion.HLSL >= ShaderVersion{5, 1}, "Shader model5.1 is unavailable");
    auto enabled = [](auto feature) { return feature == DEVICE_FEATURE_STATE_ENABLED; };
    require(stage != ShaderStage::Compute || enabled(info.Features.ComputeShaders),
            "Compute shaders are not enabled on this device");
    require(stage != ShaderStage::Geometry || enabled(info.Features.GeometryShaders),
            "Geometry shaders are not enabled on this device");
    require((stage != ShaderStage::Hull && stage != ShaderStage::Domain) ||
                enabled(info.Features.Tessellation),
            "Tessellation is not enabled on this device");
}
const char* resource_kind(SHADER_RESOURCE_TYPE type) {
    switch (type) {
    case SHADER_RESOURCE_TYPE_CONSTANT_BUFFER:
        return "constant_buffer";
    case SHADER_RESOURCE_TYPE_TEXTURE_SRV:
        return "texture_srv";
    case SHADER_RESOURCE_TYPE_BUFFER_SRV:
        return "buffer_srv";
    case SHADER_RESOURCE_TYPE_TEXTURE_UAV:
        return "texture_uav";
    case SHADER_RESOURCE_TYPE_BUFFER_UAV:
        return "buffer_uav";
    case SHADER_RESOURCE_TYPE_SAMPLER:
        return "sampler";
    default:
        throw std::runtime_error("Resource type is outside shader profile");
    }
}
const char* dimension(D3D_SRV_DIMENSION value) {
    switch (value) {
    case D3D_SRV_DIMENSION_UNKNOWN:
        return "unknown";
    case D3D_SRV_DIMENSION_BUFFER:
        return "buffer";
    case D3D_SRV_DIMENSION_BUFFEREX:
        return "bufferex";
    case D3D_SRV_DIMENSION_TEXTURE1D:
        return "texture1d";
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
        return "texture1d_array";
    case D3D_SRV_DIMENSION_TEXTURE2D:
        return "texture2d";
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        return "texture2d_array";
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
        return "texture2d_ms";
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
        return "texture2d_ms_array";
    case D3D_SRV_DIMENSION_TEXTURE3D:
        return "texture3d";
    case D3D_SRV_DIMENSION_TEXTURECUBE:
        return "texture_cube";
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        return "texture_cube_array";
    }
    throw std::runtime_error("Unknown shader resource dimension");
}
Json variables(const ShaderCodeVariableDesc* fields, Uint32 count, unsigned depth,
               std::size_t& total) {
    if (!count)
        return Json::array();
    require(depth <= 8 && count <= 4096 && fields,
            "Native shader reflection exceeds field/depth limits");
    Json result = Json::array();
    for (Uint32 i = 0; i < count; ++i) {
        require(++total <= 4096, "Native shader reflection exceeds field budget");
        const auto& f = fields[i];
        require(f.Name && f.Class > SHADER_CODE_VARIABLE_CLASS_UNKNOWN &&
                    f.Class < SHADER_CODE_VARIABLE_CLASS_COUNT &&
                    f.BasicType < SHADER_CODE_BASIC_TYPE_COUNT,
                "Invalid native shader field descriptor");
        result.push_back({{"name", f.Name},
                          {"class", GetShaderCodeVariableClassString(f.Class)},
                          {"basic", GetShaderCodeBasicTypeString(f.BasicType)},
                          {"rows", f.NumRows},
                          {"columns", f.NumColumns},
                          {"offset", f.Offset},
                          {"array_size", f.ArraySize},
                          {"members", variables(f.pMembers, f.NumMembers, depth + 1, total)}});
    }
    return result;
}
Json reflection(IShader* shader, ShaderStage expected) {
    const void* code = nullptr;
    Uint64 size = 0;
    shader->GetBytecode(&code, size);
    require(code && size && size <= 16 * 1024 * 1024, "Missing/oversized native shader bytecode");
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> native;
    require(SUCCEEDED(D3DReflect(code, static_cast<SIZE_T>(size), IID_PPV_ARGS(&native))),
            "Native bytecode reflection failed");
    D3D12_SHADER_DESC desc{};
    require(SUCCEEDED(native->GetDesc(&desc)) && reflected_stage(desc.Version) == expected,
            "Native bytecode stage differs from declared stage");
    RefCntAutoPtr<IShaderD3D12> hlsl(shader, IID_ShaderD3D12);
    require(bool(hlsl), "Native D3D12 shader reflection interface is missing");
    require(shader->GetResourceCount() <= 256, "Native shader resource count exceeds profile");
    Json resources = Json::array();
    std::size_t total = 0;
    for (Uint32 i = 0; i < shader->GetResourceCount(); ++i) {
        HLSLShaderResourceDesc r;
        hlsl->GetHLSLResource(i, r);
        require(r.Name, "Native shader resource has no name");
        D3D12_SHADER_INPUT_BIND_DESC d{};
        require(SUCCEEDED(native->GetResourceBindingDescByName(r.Name, &d)) &&
                    r.ShaderRegister == d.BindPoint && r.RegisterSpace == d.Space &&
                    r.ArraySize == d.BindCount,
                "Native shader binding reflection disagrees");
        Json value{{"name", r.Name},
                   {"kind", resource_kind(r.Type)},
                   {"register", r.ShaderRegister},
                   {"space", r.RegisterSpace},
                   {"array_size", r.ArraySize},
                   {"dimension", dimension(d.Dimension)}};
        if (r.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER) {
            const auto* b = shader->GetConstantBufferDesc(i);
            require(b, "Constant buffer reflection was not loaded");
            value["size"] = b->Size;
            value["variables"] = variables(b->pVariables, b->NumVariables, 0, total);
        }
        resources.push_back(std::move(value));
    }
    std::sort(resources.begin(), resources.end(),
              [](const auto& a, const auto& b) { return a.at("name") < b.at("name"); });
    UINT x = 0, y = 0, z = 0;
    if (expected == ShaderStage::Compute)
        native->GetThreadGroupSize(&x, &y, &z);
    Json result{
        {"resources", resources}, {"stage", shader_stage_name(expected)}, {"threads", {x, y, z}}};
    validate_shader_reflection(result);
    return result;
}
std::string diagnostic(IDataBlob* blob) {
    if (!blob || !blob->GetDataPtr())
        return "No compiler diagnostic was returned";
    const auto* text = static_cast<const char*>(blob->GetDataPtr());
    const auto count = std::min<std::size_t>(blob->GetSize(), 64 * 1024);
    std::size_t length = 0;
    while (length < count && text[length])
        ++length;
    return std::string(text, length);
}
} // namespace
std::string diligent_shader_compiler_digest() {
    const auto module = GetModuleHandleW(L"d3dcompiler_47.dll");
    require(module, "Loaded FXC compiler DLL is unavailable for provenance");
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(length && length < path.size(), "Cannot resolve the loaded FXC compiler path");
    path.resize(length);
    return content_digest(read_bytes(std::filesystem::path(path), 32 * 1024 * 1024));
}
bool diligent_shader_compiler_debug() {
#ifdef DILIGENT_DEBUG
    return true;
#else
    return false;
#endif
}
DiligentShaderProgram
compile_diligent_shader(Diligent::IRenderDevice* device, const ShaderProgramSource& program,
                        const ShaderSources& sources,
                        const std::map<std::string, std::string>& permutation) {
    const auto compiler = diligent_shader_compiler_digest();
    const auto input = shader_build_input(program, sources, permutation, compiler,
                                          diligent_shader_compiler_debug());
    const auto defines = select_shader_permutation(program, permutation);
    std::vector<MemoryShaderSourceFileInfo> files;
    for (const auto& [name, source] : sources)
        files.emplace_back(name.c_str(), source.c_str(), static_cast<Uint32>(source.size()));
    RefCntAutoPtr<IShaderSourceInputStreamFactory> factory;
    CreateMemoryShaderSourceFactory({files.data(), static_cast<Uint32>(files.size()), false},
                                    &factory);
    require(bool(factory), "Shader source snapshot factory creation failed");
    std::vector<ShaderMacro> macros;
    for (const auto& [name, value] : defines)
        macros.push_back({name.c_str(), value.c_str()});
    DiligentShaderProgram result;
    result.data.build_key = input.key();
    result.data.compiler_digest = compiler;
    result.data.row_major = program.row_major;
    result.data.compiler_debug = diligent_shader_compiler_debug();
    for (const auto& entry : program.stages) {
        device_profile(device, entry.stage);
        ShaderCreateInfo ci;
        ci.Desc.Name = entry.source.c_str();
        ci.Desc.ShaderType = native_stage(entry.stage);
        ci.FilePath = entry.source.c_str();
        ci.EntryPoint = entry.entry.c_str();
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.ShaderCompiler = SHADER_COMPILER_FXC;
        ci.HLSLVersion = {5, 1};
        ci.LoadConstantBufferReflection = true;
        ci.CompileFlags = program.row_major ? SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR
                                            : SHADER_COMPILE_FLAG_NONE;
        ci.ShaderOptimizationLevel = static_cast<SHADER_OPTIMIZATION_LEVEL>(
            SHADER_OPTIMIZATION_LEVEL_0 + program.optimization);
        ci.Macros = {macros.data(), static_cast<Uint32>(macros.size())};
        ci.pShaderSourceStreamFactory = factory;
        RefCntAutoPtr<IShader> shader;
        RefCntAutoPtr<IDataBlob> errors;
        device->CreateShader(ci, &shader, &errors);
        if (!shader)
            throw std::runtime_error(entry.source + " (" + shader_stage_name(entry.stage) +
                                     "): " + diagnostic(errors));
        ShaderStageData data;
        data.stage = entry.stage;
        data.entry = entry.entry;
        data.reflection = reflection(shader, entry.stage);
        const void* code = nullptr;
        Uint64 size = 0;
        shader->GetBytecode(&code, size);
        const auto* begin = static_cast<const std::byte*>(code);
        data.bytecode.assign(begin, begin + size);
        result.data.stages.push_back(std::move(data));
        result.stages.emplace(entry.stage, std::move(shader));
    }
    validate_shader(result.data);
    return result;
}
DiligentShaderProgram realize_diligent_shader(Diligent::IRenderDevice* device,
                                              const ShaderData& data) {
    validate_shader(data);
    DiligentShaderProgram result;
    result.data = data;
    for (const auto& entry : data.stages) {
        device_profile(device, entry.stage);
        ShaderCreateInfo ci;
        ci.Desc.Name = "FORGE cooked shader";
        ci.Desc.ShaderType = native_stage(entry.stage);
        ci.ByteCode = entry.bytecode.data();
        ci.ByteCodeSize = entry.bytecode.size();
        ci.LoadConstantBufferReflection = true;
        RefCntAutoPtr<IShader> shader;
        device->CreateShader(ci, &shader);
        require(bool(shader), "Cooked shader creation failed");
        require(reflection(shader, entry.stage) == entry.reflection,
                "Cooked shader reflection differs from its admitted metadata");
        result.stages.emplace(entry.stage, std::move(shader));
    }
    return result;
}
} // namespace forge::asset_detail
