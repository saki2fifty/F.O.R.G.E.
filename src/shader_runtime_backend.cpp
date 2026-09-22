#include "shader_runtime_backend.hpp"
#include "presentation_diligent.hpp"
#include <stdexcept>
namespace forge {
asset_detail::DiligentShaderProgram realize_renderer_shader(DiligentPresentation& presentation,
                                                            const ShaderData& shader) {
    auto* device = presentation.device();
    if (!device)
        throw std::runtime_error("Cooked Shader requires a renderer device");
#ifdef FORGE_SHADER_DXBC
    if (device->GetDeviceInfo().Type == Diligent::RENDER_DEVICE_TYPE_D3D12)
        return asset_detail::realize_diligent_shader(
            device, shader,
            [&](const auto& info, auto** result) { presentation.shader(info, result); });
#else
    (void)shader;
#endif
    throw std::runtime_error("The selected Shader artifact is cooked for D3D12/FXC; this backend "
                             "requires its own Shader cook adapter");
}
} // namespace forge
