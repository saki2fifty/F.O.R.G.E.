# Official stable Draco1.5.7. Native decode stays in private asset tooling;
# Diligent's optional TinyGLTF Draco bridge remains disabled at its retained pin.
foreach(option DRACO_FAST DRACO_JS_GLUE DRACO_IE_COMPATIBLE DRACO_POINT_CLOUD_COMPRESSION
 DRACO_PREDICTIVE_EDGEBREAKER DRACO_BACKWARDS_COMPATIBILITY DRACO_DECODER_ATTRIBUTE_DEDUPLICATION
 DRACO_TESTS DRACO_WASM DRACO_UNITY_PLUGIN DRACO_ANIMATION_ENCODING DRACO_MAYA_PLUGIN
 DRACO_TRANSCODER_SUPPORTED DRACO_INSTALL)
 set(${option} OFF CACHE BOOL "Private glTF mesh codec" FORCE)
endforeach()
set(DRACO_GLTF_BITSTREAM ON CACHE BOOL "Ratified glTF Draco bitstream profile" FORCE)
set(DRACO_MESH_COMPRESSION ON CACHE BOOL "Native mesh codec" FORCE)
set(DRACO_STANDARD_EDGEBREAKER ON CACHE BOOL "Native glTF mesh codec" FORCE)
if(FORGE_ENABLE_SANITIZERS)
 set(DRACO_SANITIZE "address,undefined" CACHE STRING "Instrument native codec objects" FORCE)
else()
 set(DRACO_SANITIZE "" CACHE STRING "" FORCE)
endif()
set(_forge_shared_before_draco "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(draco
 URL https://codeload.github.com/google/draco/tar.gz/8786740086a9f4d83f44aa83badfbea4dce7a1b5
 URL_HASH SHA256=b9c2392dbfcf454aaec68823d832de9d62614054b33807b7c9776799b8e0bbca
 EXCLUDE_FROM_ALL)
# Upstream tests compiler flags using an executable try-compile. Sanitizer
# instrumentation also needs its runtime at that probe's link step.
include(CMakePushCheckState)
cmake_push_check_state()
if(FORGE_ENABLE_SANITIZERS)
 list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
endif()
FetchContent_MakeAvailable(draco)
cmake_pop_check_state()
set(BUILD_SHARED_LIBS "${_forge_shared_before_draco}")
