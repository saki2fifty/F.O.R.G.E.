# Shared Flecs is an explicit profile; never link a private static copy into an SDK module.
set(FORGE_LINKAGE_PROFILE "static-core")
if(FORGE_ENABLE_NATIVE_SDK)
 set(FORGE_LINKAGE_PROFILE "shared-native-sdk")
 if(MSVC)
  set(_forge_crt MultiThreadedDLL)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set(_forge_crt MultiThreadedDebugDLL)
  endif()
  if(CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
   set(CMAKE_MSVC_RUNTIME_LIBRARY "${_forge_crt}")
  endif()
  if(NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL _forge_crt)
   message(FATAL_ERROR "Native SDK requires matching /MD Release or /MDd Debug CRT")
  endif()
 endif()
endif()
if(FORGE_ENABLE_NATIVE_SDK AND NOT CMAKE_BUILD_TYPE MATCHES "^(Release|Debug|RelWithDebInfo|MinSizeRel)$")
 message(FATAL_ERROR "Select an explicit build configuration")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _forge_config)
# One explicit installed value boundary drives installation and compatibility hashing.
set(FORGE_SDK_HEADERS native_sdk.h sdk_client.hpp input_message.hpp identity.hpp asset_ref.hpp physics_components.hpp character_components.hpp audio_components.hpp animation_components.hpp navigation_components.hpp navigation_behavior_components.hpp ui_components.hpp ui_values.hpp render_components.hpp reflection_probe.hpp decal.hpp sprite_frames.hpp material_parameter.hpp transform_components.hpp model_asset.hpp engine_assets.hpp engine_texture_assets.hpp texture_dimension.hpp primitive_catalog.hpp)
list(APPEND FORGE_SDK_HEADERS application_state_components.hpp application_state_ecs.hpp application_state_key.hpp)
list(APPEND FORGE_SDK_HEADERS mesh_wind.hpp)
list(APPEND FORGE_SDK_HEADERS fog_volume.hpp)
set(FORGE_SDK_CONTRACT_HASHES "")
foreach(header IN LISTS FORGE_SDK_HEADERS)
 set(path "${PROJECT_SOURCE_DIR}/include/forge/${header}")
 # Catch omitted transitive SDK headers during configuration, before installing
 # a package that only compiles while private source headers remain reachable.
 file(STRINGS "${path}" _forge_sdk_includes REGEX [[^#[ \t]*include[ \t]+[<"]forge/]])
 foreach(line IN LISTS _forge_sdk_includes)
  string(REGEX REPLACE [[.*[<"]forge/([^>"]+)[>"].*]] "\\1" included "${line}")
  if(NOT included IN_LIST FORGE_SDK_HEADERS AND NOT included STREQUAL "native_sdk_identity.h")
   message(FATAL_ERROR "Installed SDK header ${header} depends on omitted ${included}")
  endif()
 endforeach()
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
 file(SHA256 "${path}" hash)
 string(APPEND FORGE_SDK_CONTRACT_HASHES "${header}:${hash};")
endforeach()
# Exact bridge and value-support semantics are part of this private SDK contract.
foreach(source
 include/forge/application_states.hpp include/forge/application_state_definition.hpp
 src/input_message_runtime.cpp src/input_message_runtime.hpp
 src/simple_polygon.hpp src/navigation_region.hpp src/navigation_region_value.hpp
 src/physics_collider_2d.hpp src/physics_collider_2d_native.hpp src/physics_collider_2d.cpp
 src/navigation_collision_geometry.hpp src/navigation_collision_source.hpp
 src/navigation_tile_geometry.hpp src/navigation_tile_geometry.cpp
 include/forge/application_state_activation.hpp
 include/forge/application_state_source.hpp src/application_states.cpp
 src/application_state_definition.cpp src/application_state_source.cpp
 src/application_state_resource.hpp src/single_source_admission.hpp
 include/forge/ui_localization.hpp src/ui_localization.cpp
 src/ui_values_json.hpp
 include/forge/runtime_resource_service.hpp include/forge/navigation_behavior.hpp
 src/navigation_behavior.cpp src/navigation_behavior_tree.cpp src/navigation_behavior_tree.hpp
 src/navigation_behavior_resource.hpp src/navigation_behavior_admission.hpp
 cmake/box2d_snapshot_guards.cmake include/forge/animation_property.hpp src/native_sdk.cpp src/runtime_entity_creation.cpp src/runtime_entity_creation.hpp src/runtime_resources.cpp src/runtime_tile_map.cpp src/runtime_tile_map.hpp src/tile_map_data.cpp src/tile_map_data.hpp src/tile_map_variants.hpp src/tile_map_variants.cpp src/tile_map_values.hpp src/tile_set_data.cpp src/tile_set_data.hpp src/tile_set_values.hpp src/tile_polygon_decomposition.hpp src/tile_polygon_contours.hpp src/tile_animation.hpp src/tile_neighbors.hpp src/tile_grid.hpp src/identity.cpp src/authored_schema.cpp src/authored_schema.hpp src/authored_component.cpp src/authored_component.hpp src/authored_generation.cpp src/authored_generation.hpp src/json_value_equal.hpp src/reflected_value.hpp src/reflected_string.hpp src/reconstructed_meta.hpp src/reflected_extensions.hpp src/reflected_value.cpp src/reflected_native.cpp src/reconstructed_meta.cpp src/reflected_extensions.cpp)
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${source}")
 file(SHA256 "${PROJECT_SOURCE_DIR}/${source}" hash)
 string(APPEND FORGE_SDK_CONTRACT_HASHES "${source}:${hash};")
endforeach()
string(SHA256 FORGE_NATIVE_SDK_FINGERPRINT "exact2;flecs-script-math=1;visual-scale=signed-zero-v1;${FORGE_SDK_CONTRACT_HASHES};flecs-fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8;${FORGE_LINKAGE_PROFILE};${CMAKE_SYSTEM_NAME};${CMAKE_SYSTEM_PROCESSOR};${CMAKE_SIZEOF_VOID_P};${CMAKE_CXX_COMPILER_ID};${CMAKE_CXX_COMPILER_VERSION};${MSVC_TOOLSET_VERSION};${CMAKE_BUILD_TYPE};${CMAKE_MSVC_RUNTIME_LIBRARY};${CMAKE_C_FLAGS};${CMAKE_CXX_FLAGS};${CMAKE_C_FLAGS_${_forge_config}};${CMAKE_CXX_FLAGS_${_forge_config}};${FORGE_ENABLE_SANITIZERS}")
configure_file(cmake/native_sdk_identity.h.in generated/forge/native_sdk_identity.h @ONLY)
