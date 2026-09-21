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
add_executable(forge_vulkan_binding_probe tests/vulkan_binding_probe.cpp src/render_backend.cpp
 src/sampler_backend.cpp src/texture_gpu.cpp)
target_compile_definitions(forge_vulkan_binding_probe PRIVATE FORGE_VULKAN_LIMIT_QUERY=1)
target_include_directories(forge_vulkan_binding_probe PRIVATE src
 "${diligent_SOURCE_DIR}/DiligentCore")
target_link_libraries(forge_vulkan_binding_probe PRIVATE forge_material
 forge_texture_formats Diligent-GraphicsEngineVk-static Diligent-BuildSettings
 Vulkan::Headers ${CMAKE_DL_LIBS})
add_test(NAME vulkan_material_binding COMMAND ${Python3_EXECUTABLE}
 "${PROJECT_SOURCE_DIR}/tests/vulkan_binding_probe.py" $<TARGET_FILE:forge_vulkan_binding_probe>
 "${FORGE_VULKAN_PROBE_DXC}" "${CMAKE_BINARY_DIR}/vulkan-binding-probe")
set_tests_properties(vulkan_material_binding PROPERTIES TIMEOUT 90)
