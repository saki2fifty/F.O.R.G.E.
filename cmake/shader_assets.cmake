# Immutable recipe provenance. This target remains UI/device independent; native
# compilation lives in the Windows worker, whose adapter sources count in the key.
set(_forge_shader_recipe_inputs "${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_DEBUG};${CMAKE_CXX_FLAGS_RELEASE};${CMAKE_CXX_FLAGS_RELWITHDEBINFO};${CMAKE_MSVC_RUNTIME_LIBRARY};${FORGE_ENABLE_SANITIZERS};${CMAKE_SYSTEM_NAME};${CMAKE_SYSTEM_PROCESSOR}")
foreach(source
 include/forge/shader_asset.hpp src/shader_asset.cpp src/shader_pipeline.hpp src/shader_pipeline.cpp
 src/shader_importer.cpp src/shader_diligent.hpp src/shader_diligent.cpp src/shader_worker_main.cpp
 src/import_process.hpp src/import_process.cpp src/asset_worker.hpp src/asset_worker.cpp
 src/cooked_envelope.hpp src/bounded_json.hpp cmake/shader_assets.cmake cmake/diligent_source.cmake)
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
 file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source}" digest)
 string(APPEND _forge_shader_recipe_inputs ";${source}:${digest}")
endforeach()
string(SHA256 _forge_shader_recipe_fingerprint "${_forge_shader_recipe_inputs}")
target_compile_definitions(forge_shader PRIVATE
 FORGE_SHADER_RECIPE_FINGERPRINT="${_forge_shader_recipe_fingerprint}"
 FORGE_SHADER_RECIPE_CONFIGURATION="$<CONFIG>")
