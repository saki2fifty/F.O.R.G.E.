# Exact source recipe identity for bounded CPU material resolution and admission.
set(_forge_material_recipe_inputs "${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${FORGE_ENABLE_SANITIZERS};${CMAKE_SYSTEM_NAME}")
foreach(source
 include/forge/material_asset.hpp include/forge/material_source.hpp
 include/forge/surface_shader.hpp include/forge/shader_asset.hpp src/surface_shader.cpp src/shader_asset.cpp
 src/material_asset.cpp src/material_source.cpp src/pbr_material.cpp src/pbr_material.hpp
 src/material_authoring.cpp src/material_selection.cpp src/material_selection.hpp
 src/model_render_resource.cpp src/model_selection.cpp src/texture_resource.cpp
 src/material_resource.cpp src/engine_render_resource.cpp
 src/bounded_json.hpp src/cooked_envelope.hpp cmake/material_assets.cmake)
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
 file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source}" digest)
 string(APPEND _forge_material_recipe_inputs ";${source}:${digest}")
endforeach()
string(SHA256 _forge_material_recipe_fingerprint "${_forge_material_recipe_inputs}")
target_compile_definitions(forge_material_authoring PRIVATE FORGE_MATERIAL_RECIPE_FINGERPRINT="${_forge_material_recipe_fingerprint}")
