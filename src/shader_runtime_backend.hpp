#pragma once
#include "shader_diligent.hpp"
namespace forge {
class DiligentPresentation;
// Cooked GPU artifacts have a target profile. Logical Shader/Material identities
// do not. Keep native reflection/cook realization behind this narrow adapter.
asset_detail::DiligentShaderProgram realize_renderer_shader(DiligentPresentation&,
                                                            const ShaderData&);
} // namespace forge
