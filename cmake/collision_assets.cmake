# Portable collision data; exact native cooker/profile remains in recipe identity.
set(_forge_collision_recipe_inputs "jolt:e77f175595e64cb44218cc9d9d56fc365ad0e36a;${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${FORGE_ENABLE_SANITIZERS};${CMAKE_SYSTEM_NAME}")
foreach(source
 include/forge/collision_asset.hpp include/forge/collision_generation.hpp include/forge/collision_source.hpp
 src/collision_asset.cpp src/collision_generation.cpp src/collision_source.cpp src/collision_validation.hpp
 src/collision_bundle.cpp src/collision_bundle.hpp src/collision_authoring.cpp src/collision_shape.cpp
 src/model_render_resource.cpp src/model_selection.cpp src/engine_render_resource.cpp
 src/bounded_json.hpp src/cooked_envelope.hpp cmake/collision_assets.cmake cmake/dependencies.cmake)
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
 file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source}" digest)
 string(APPEND _forge_collision_recipe_inputs ";${source}:${digest}")
endforeach()
string(SHA256 _forge_collision_recipe_fingerprint "${_forge_collision_recipe_inputs}")
target_compile_definitions(forge_collision_authoring PRIVATE FORGE_COLLISION_RECIPE_FINGERPRINT="${_forge_collision_recipe_fingerprint}")
