# Exact original source identity for tile value/cook/dependency/profile admission.
set(_forge_tile_recipe_inputs "${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${FORGE_ENABLE_SANITIZERS};${CMAKE_SYSTEM_NAME};tile-assets-v1")
foreach(source
 src/tile_asset_types.hpp src/tile_asset_recipe.cpp
 src/tile_asset_authoring.hpp src/tile_asset_authoring.cpp
 src/tile_asset_resources.hpp src/tile_asset_resources.cpp
 src/tile_asset_worker.hpp src/tile_asset_worker.cpp
 src/tile_grid.hpp src/tile_map_values.hpp src/tile_map_source.hpp src/tile_map_source.cpp
 src/tile_map_data.hpp src/tile_map_data.cpp src/tile_map_cook.hpp src/tile_map_cook.cpp
 src/tile_set_values.hpp src/tile_set_source.hpp src/tile_set_source.cpp
 src/tile_set_data.hpp src/tile_set_data.cpp src/tile_set_cook.hpp src/tile_set_cook.cpp
 src/sprite_region.hpp src/sprite_material_profile.hpp src/sprite_draw_selection.hpp
 src/model_draw_candidate.hpp src/model_draw_candidate.cpp src/model_render_resource.cpp
 src/import_process.hpp src/import_process.cpp src/import_worker_main.cpp
 src/bounded_json.hpp src/cooked_envelope.hpp cmake/tile_assets.cmake)
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
 file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source}" digest)
 string(APPEND _forge_tile_recipe_inputs ";${source}:${digest}")
endforeach()
string(SHA256 _forge_tile_recipe_fingerprint "${_forge_tile_recipe_inputs}")
target_compile_definitions(forge_tile_asset_recipe PRIVATE FORGE_TILE_RECIPE_FINGERPRINT="${_forge_tile_recipe_fingerprint}")
