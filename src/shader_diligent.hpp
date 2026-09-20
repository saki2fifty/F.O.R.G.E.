#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <forge/shader_asset.hpp>
namespace forge::asset_detail {
struct DiligentShaderProgram {
    ShaderData data;
    std::map<ShaderStage, Diligent::RefCntAutoPtr<Diligent::IShader>> stages;
};
// Actual loaded FXC DLL, not a user label. Included in every compiled artifact key.
std::string diligent_shader_compiler_digest();
bool diligent_shader_compiler_debug();
// Detached native preparation. No selected material/pipeline/resource changes.
// Project-source callers must execute this inside the bounded shader worker.
DiligentShaderProgram compile_diligent_shader(Diligent::IRenderDevice*, const ShaderProgramSource&,
                                              const ShaderSources&,
                                              const std::map<std::string, std::string>&);
// Re-reflects native bytecode and compares copied metadata before adoption. No
// source compiler is invoked by this cooked-runtime path.
DiligentShaderProgram realize_diligent_shader(Diligent::IRenderDevice*, const ShaderData&);
} // namespace forge::asset_detail
