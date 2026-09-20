# Exact pinned native PBR utilities, without FX's EnTT scene graph or ImGui link.
# Sources and shader generation are upstream implementations, not local patches.
set(forge_fx_root "${diligent_SOURCE_DIR}/DiligentFX")
file(GLOB_RECURSE forge_fx_shaders CONFIGURE_DEPENDS LIST_DIRECTORIES false
 "${forge_fx_root}/Shaders/*.*")
set(forge_fx_generated "${CMAKE_CURRENT_BINARY_DIR}/forge-pbr-shaders")
convert_shaders_to_headers("${forge_fx_shaders}" "${forge_fx_generated}"
 "${forge_fx_generated}/shaders_list.h" forge_fx_shader_headers)
add_library(forge_diligent_pbr_native STATIC
 "${forge_fx_root}/PBR/src/PBR_Renderer.cpp"
 "${forge_fx_root}/Utilities/src/DiligentFXShaderSourceStreamFactory.cpp"
 ${forge_fx_shader_headers} "${forge_fx_generated}/shaders_list.h")
target_include_directories(forge_diligent_pbr_native PUBLIC "${forge_fx_root}"
 "${forge_fx_root}/PBR/interface" PRIVATE "${forge_fx_generated}")
target_link_libraries(forge_diligent_pbr_native PUBLIC Diligent-GraphicsEngine
 Diligent-GraphicsTools PRIVATE Diligent-BuildSettings)
add_library(forge_presentation_diligent STATIC src/presentation_diligent.cpp)
target_include_directories(forge_presentation_diligent PUBLIC src
 "${diligent_SOURCE_DIR}/DiligentCore")
target_compile_definitions(forge_presentation_diligent PRIVATE NOMINMAX)
target_link_libraries(forge_presentation_diligent PUBLIC forge_diligent_pbr_native
 PRIVATE Diligent-Archiver-shared Diligent-BuildSettings)
