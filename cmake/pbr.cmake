# Exact pinned native PBR utilities, without FX's EnTT scene graph or ImGui link.
# Native C++ remains unmodified. Two reviewed shader corrections are staged separately.
set(forge_fx_root "${diligent_SOURCE_DIR}/DiligentFX")
file(GLOB_RECURSE forge_fx_shaders CONFIGURE_DEPENDS LIST_DIRECTORIES false
 "${forge_fx_root}/Shaders/*.*")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(forge_fx_patch_manifest "${PROJECT_SOURCE_DIR}/cmake/patches/diligentfx-aaa41d47-shader-warnings.json")
set(forge_fx_patch "${PROJECT_SOURCE_DIR}/cmake/patches/diligentfx-aaa41d47-shader-warnings.patch")
set(forge_fx_patched "${CMAKE_CURRENT_BINARY_DIR}/forge-patched-fx")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
 "${forge_fx_patch_manifest}" "${forge_fx_patch}" "${PROJECT_SOURCE_DIR}/tools/stage_diligentfx_patch.py"
 "${forge_fx_root}/Shaders/PBR/private/Iridescence.fxh"
 "${forge_fx_root}/Shaders/Common/public/PBR_Common.fxh")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/stage_diligentfx_patch.py"
 "${diligent_SOURCE_DIR}" "${forge_fx_patched}"
 RESULT_VARIABLE forge_fx_patch_result OUTPUT_VARIABLE forge_fx_patch_record)
if(NOT forge_fx_patch_result EQUAL 0)
 message(FATAL_ERROR "DiligentFX reviewed shader patch failed; source/pin requires review")
endif()
foreach(shader IN ITEMS "Shaders/PBR/private/Iridescence.fxh" "Shaders/Common/public/PBR_Common.fxh")
 list(REMOVE_ITEM forge_fx_shaders "${forge_fx_root}/${shader}")
 list(APPEND forge_fx_shaders "${forge_fx_patched}/${shader}")
endforeach()
if(BUILD_TESTING)
 add_test(NAME diligentfx_patch COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/diligentfx_patch_test.py" "${diligent_SOURCE_DIR}")
endif()
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
include(cmake/presentation_sources.cmake)
add_library(forge_presentation_diligent STATIC ${forge_presentation_sources})
target_include_directories(forge_presentation_diligent PUBLIC src
 "${diligent_SOURCE_DIR}/DiligentCore")
target_compile_definitions(forge_presentation_diligent PRIVATE NOMINMAX)
target_compile_definitions(forge_presentation_diligent PRIVATE FORGE_SHADER_DXBC)
target_link_libraries(forge_presentation_diligent PRIVATE forge_shader_diligent)
target_link_libraries(forge_presentation_diligent PUBLIC forge_render_bounds forge_model_render_resources forge_mesh_resources forge_material_resources forge_texture_resources forge_diligent_pbr_native Diligent-BuildSettings
 PRIVATE forge_texture_formats Diligent-Archiver-shared)
