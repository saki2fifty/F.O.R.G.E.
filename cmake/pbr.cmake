# Exact pinned native PBR utilities, without FX's EnTT scene graph or ImGui link.
# Sources and shader generation are upstream implementations, not local patches.
set(forge_fx_root "${diligent_SOURCE_DIR}/DiligentFX")
file(GLOB_RECURSE forge_fx_shaders CONFIGURE_DEPENDS LIST_DIRECTORIES false
 "${forge_fx_root}/Shaders/*.*")
list(APPEND forge_fx_shaders "${PROJECT_SOURCE_DIR}/resources/shaders/ForgeSurface.fxh"
 "${PROJECT_SOURCE_DIR}/resources/shaders/ForgeLighting.fxh"
 "${PROJECT_SOURCE_DIR}/resources/shaders/ForgeShadows.fxh"
 "${PROJECT_SOURCE_DIR}/resources/shaders/ForgeTransmission.fxh")
set(forge_fx_generated "${CMAKE_CURRENT_BINARY_DIR}/forge-pbr-shaders")
convert_shaders_to_headers("${forge_fx_shaders}" "${forge_fx_generated}"
 "${forge_fx_generated}/shaders_list.h" forge_fx_shader_headers)
add_library(forge_diligent_pbr_native STATIC
 "${forge_fx_root}/PBR/src/PBR_Renderer.cpp"
 "${forge_fx_root}/Components/src/EnvMapRenderer.cpp"
 "${forge_fx_root}/Components/src/ShadowMapManager.cpp"
 "${forge_fx_root}/Utilities/src/DiligentFXShaderSourceStreamFactory.cpp"
 ${forge_fx_shader_headers} "${forge_fx_generated}/shaders_list.h")
target_include_directories(forge_diligent_pbr_native PUBLIC "${forge_fx_root}"
 "${forge_fx_root}/PBR/interface" "${forge_fx_root}/Components/interface" PRIVATE "${forge_fx_generated}")
target_link_libraries(forge_diligent_pbr_native PUBLIC Diligent-GraphicsEngine
 Diligent-GraphicsTools Diligent-BuildSettings)
include(cmake/texture_formats.cmake)
add_library(forge_presentation_diligent STATIC src/presentation_diligent.cpp src/environment_gpu.cpp src/environment_sky.cpp src/frame_renderer.cpp src/shadow_view.cpp src/shadow_renderer.cpp src/transmission_background.cpp src/display_resolve.cpp src/texture_preview.cpp src/texture_gpu.cpp src/mesh_gpu.cpp src/mesh_vertex_fetch.cpp src/mesh_draw.cpp src/mesh_draw_shader.cpp src/mesh_draw_bundle.cpp src/mesh_render_host.cpp src/gpu_residency.cpp)
target_include_directories(forge_presentation_diligent PUBLIC src
 "${diligent_SOURCE_DIR}/DiligentCore")
target_compile_definitions(forge_presentation_diligent PRIVATE NOMINMAX)
target_link_libraries(forge_presentation_diligent PUBLIC forge_render_bounds forge_model_render_resources forge_mesh_resources forge_material_resources forge_texture_resources forge_diligent_pbr_native Diligent-BuildSettings
 PRIVATE forge_texture_formats Diligent-Archiver-shared)
