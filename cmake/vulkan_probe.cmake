# Optional Linux portability evidence, independent of the Windows editor host.
# Uses the already selected Diligent Vulkan backend and an explicit external
# compiler. No compiler is downloaded or invoked by the shipped runtime.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT FORGE_BUILD_ASSET_TOOLS OR NOT BUILD_TESTING)
 message(FATAL_ERROR "FORGE_BUILD_VULKAN_PROBES requires Linux, asset tools and BUILD_TESTING")
endif()
set(FORGE_VULKAN_PROBE_DXC "" CACHE FILEPATH "Explicit DXC executable for SPIR-V portability tests")
if(NOT EXISTS "${FORGE_VULKAN_PROBE_DXC}")
 message(FATAL_ERROR "Set FORGE_VULKAN_PROBE_DXC to the verified DXC executable")
endif()
include(cmake/texture_formats.cmake)
# Compile every shared rendering unit under the Linux/Vulkan build definitions.
# This does not link or claim execution of the complete frame renderer.
include(cmake/presentation_sources.cmake)
add_library(forge_vulkan_renderer_compile OBJECT ${forge_presentation_sources})
target_include_directories(forge_vulkan_renderer_compile PRIVATE src
 "${diligent_SOURCE_DIR}/DiligentCore" "${diligent_SOURCE_DIR}/DiligentFX"
 "${diligent_SOURCE_DIR}/DiligentFX/PBR/interface"
 "${diligent_SOURCE_DIR}/DiligentFX/Components/interface")
target_compile_definitions(forge_vulkan_renderer_compile PRIVATE NOMINMAX FORGE_VULKAN_LIMIT_QUERY=1)
target_link_libraries(forge_vulkan_renderer_compile PRIVATE forge_render_bounds
 forge_model_render_resources forge_mesh_resources forge_material_resources forge_texture_resources
 forge_texture_formats Diligent-GraphicsEngine Diligent-BuildSettings Vulkan::Headers)
target_link_libraries(forge_vulkan_renderer_compile PRIVATE forge_particle_effect_resources forge_particle_render)
add_executable(forge_vulkan_binding_probe tests/vulkan_binding_probe.cpp src/render_backend.cpp
 src/sampler_backend.cpp src/texture_gpu.cpp)
target_compile_definitions(forge_vulkan_binding_probe PRIVATE FORGE_VULKAN_LIMIT_QUERY=1)
target_include_directories(forge_vulkan_binding_probe PRIVATE src
 "${diligent_SOURCE_DIR}/DiligentCore")
target_link_libraries(forge_vulkan_binding_probe PRIVATE forge_material
 forge_texture_formats Diligent-GraphicsEngineVk-static Diligent-BuildSettings
 Vulkan::Headers ${CMAKE_DL_LIBS})
add_dependencies(forge_vulkan_binding_probe forge_vulkan_renderer_compile)
add_test(NAME vulkan_material_binding COMMAND ${Python3_EXECUTABLE}
 "${PROJECT_SOURCE_DIR}/tests/vulkan_binding_probe.py" $<TARGET_FILE:forge_vulkan_binding_probe>
 "${FORGE_VULKAN_PROBE_DXC}" "${CMAKE_BINARY_DIR}/vulkan-binding-probe")
set_tests_properties(vulkan_material_binding PROPERTIES TIMEOUT 90)
add_executable(forge_hdr_bloom_vulkan_probe tests/hdr_bloom_vulkan_probe.cpp
 src/hdr_bloom.cpp src/render_backend.cpp)
target_include_directories(forge_hdr_bloom_vulkan_probe PRIVATE src
 "${diligent_SOURCE_DIR}/DiligentCore" "${diligent_SOURCE_DIR}/DiligentFX/PBR/interface")
target_link_libraries(forge_hdr_bloom_vulkan_probe PRIVATE forge_core
 Diligent-GraphicsEngineVk-static Diligent-BuildSettings ${CMAKE_DL_LIBS})
add_dependencies(forge_hdr_bloom_vulkan_probe forge_vulkan_renderer_compile)
add_test(NAME hdr_bloom_vulkan COMMAND ${Python3_EXECUTABLE}
 "${PROJECT_SOURCE_DIR}/tests/hdr_bloom_vulkan_probe.py" $<TARGET_FILE:forge_hdr_bloom_vulkan_probe>
 "${FORGE_VULKAN_PROBE_DXC}" "${CMAKE_BINARY_DIR}/hdr-bloom-vulkan")
set_tests_properties(hdr_bloom_vulkan PROPERTIES TIMEOUT 90)
if(FORGE_BUILD_VULKAN_RENDERER_PROBE)
 if(FORGE_BUILD_UI_PRESENTER)
  add_executable(forge_ui_vulkan_probe tests/ui_render_tests.cpp src/ui_diligent.cpp)
  target_compile_definitions(forge_ui_vulkan_probe PRIVATE FORGE_UI_VULKAN_TEST)
  target_include_directories(forge_ui_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
  target_link_libraries(forge_ui_vulkan_probe PRIVATE forge_ui_presenter PNG::PNG RmlUi::Core Diligent-GraphicsEngineVk-static Diligent-BuildSettings)
  add_test(NAME ui_vulkan COMMAND forge_ui_vulkan_probe "${CMAKE_BINARY_DIR}/ui-vulkan-images" "${PROJECT_SOURCE_DIR}/resources/ui/LatoLatin-Regular.ttf" "${PROJECT_SOURCE_DIR}/samples/projects/FeatureGallery")
  set_tests_properties(ui_vulkan PROPERTIES TIMEOUT 120)
 endif()
 include(cmake/pbr.cmake)
 add_executable(forge_tile_asset_vulkan_probe tests/tile_asset_vulkan_probe.cpp)
 target_include_directories(forge_tile_asset_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
 target_link_libraries(forge_tile_asset_vulkan_probe PRIVATE forge_presentation_diligent forge_tile_asset_authoring forge_material_authoring forge_tile_map_cook forge_tile_set_cook Diligent-GraphicsEngineVk-static)
 add_dependencies(forge_tile_asset_vulkan_probe forge_asset_build_worker)
 add_test(NAME tile_asset_vulkan COMMAND forge_tile_asset_vulkan_probe $<TARGET_FILE:forge_asset_build_worker> "${CMAKE_BINARY_DIR}/tile-asset-vulkan")
 set_tests_properties(tile_asset_vulkan PROPERTIES TIMEOUT 90)
 add_executable(forge_view_image_vulkan_probe tests/view_image_vulkan_probe.cpp)
 target_include_directories(forge_view_image_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
 target_link_libraries(forge_view_image_vulkan_probe PRIVATE forge_presentation_diligent forge_image_codecs Diligent-GraphicsEngineVk-static)
 add_test(NAME view_image_vulkan COMMAND forge_view_image_vulkan_probe "${CMAKE_BINARY_DIR}/view-image-vulkan")
 set_tests_properties(view_image_vulkan PROPERTIES TIMEOUT 30)
 add_executable(forge_sprite_vulkan_probe tests/sprite_vulkan_probe.cpp)
 target_include_directories(forge_sprite_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
 target_link_libraries(forge_sprite_vulkan_probe PRIVATE forge_presentation_diligent forge_simulation forge_game_content forge_view_image Diligent-GraphicsEngineVk-static)
 add_test(NAME sprite_vulkan COMMAND forge_sprite_vulkan_probe "${CMAKE_BINARY_DIR}/sprite-vulkan")
 set_tests_properties(sprite_vulkan PROPERTIES TIMEOUT 60)
 add_executable(forge_terrain_vulkan_probe tests/terrain_vulkan_probe.cpp)
 target_include_directories(forge_terrain_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
 target_link_libraries(forge_terrain_vulkan_probe PRIVATE forge_presentation_diligent forge_terrain_authoring Diligent-GraphicsEngineVk-static)
 add_test(NAME terrain_vulkan COMMAND forge_terrain_vulkan_probe ${CMAKE_BINARY_DIR}/terrain-vulkan-probe)
 set_tests_properties(terrain_vulkan PROPERTIES TIMEOUT 180)
 add_executable(forge_particle_vulkan_probe tests/particle_vulkan_probe.cpp)
 target_include_directories(forge_particle_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
 target_link_libraries(forge_particle_vulkan_probe PRIVATE forge_presentation_diligent forge_particle_effect_authoring forge_simulation forge_game_content Diligent-GraphicsEngineVk-static)
 add_test(NAME particle_vulkan COMMAND forge_particle_vulkan_probe "${CMAKE_BINARY_DIR}/particle-vulkan-probe" "${PROJECT_SOURCE_DIR}/samples/projects/FeatureGallery")
 set_tests_properties(particle_vulkan PROPERTIES TIMEOUT 180)
 add_executable(forge_material_parameter_vulkan_probe tests/material_parameter_vulkan_probe.cpp)
 target_include_directories(forge_material_parameter_vulkan_probe PRIVATE src "${diligent_SOURCE_DIR}/DiligentCore")
 target_link_libraries(forge_material_parameter_vulkan_probe PRIVATE forge_presentation_diligent forge_simulation forge_game_content Diligent-GraphicsEngineVk-static)
 add_test(NAME material_parameter_vulkan COMMAND forge_material_parameter_vulkan_probe "${CMAKE_BINARY_DIR}/object-material-vulkan" "${PROJECT_SOURCE_DIR}/samples/projects/FeatureGallery")
 set_tests_properties(material_parameter_vulkan PROPERTIES TIMEOUT 180)
endif()
