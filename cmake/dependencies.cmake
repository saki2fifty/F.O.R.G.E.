include(FetchContent)
if(FORGE_ENABLE_NATIVE_SDK)
 set(FLECS_STATIC OFF CACHE BOOL "" FORCE)
 set(FLECS_SHARED ON CACHE BOOL "" FORCE)
 set(FORGE_FLECS_TARGET flecs)
else()
 set(FLECS_STATIC ON CACHE BOOL "" FORCE)
 set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
 set(FORGE_FLECS_TARGET flecs_static)
endif()
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(flecs GIT_REPOSITORY https://github.com/SanderMertens/flecs.git GIT_TAG fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8) # v4.1.6
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG 55f93686c01528224f448c19128836e7df245f72)
FetchContent_MakeAvailable(flecs json)

if(FORGE_ENABLE_NATIVE_SDK)
 set_target_properties(flecs PROPERTIES VERSION 4.1.6 SOVERSION 4
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
 set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
 set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# Jolt is a private runtime implementation; no Jolt headers enter the gameplay SDK.
foreach(option OVERRIDE_CXX_FLAGS INTERPROCEDURAL_OPTIMIZATION JPH_BUILD_SHARED_LIBS
 GENERATE_DEBUG_SYMBOLS ENABLE_ALL_WARNINGS ENABLE_INSTALL DEBUG_RENDERER_IN_DEBUG_AND_RELEASE
 DEBUG_RENDERER_IN_DISTRIBUTION PROFILER_IN_DEBUG_AND_RELEASE PROFILER_IN_DISTRIBUTION
 JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE TARGET_UNIT_TESTS TARGET_HELLO_WORLD
 TARGET_PERFORMANCE_TEST TARGET_SAMPLES TARGET_VIEWER
 USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
 set(${option} OFF CACHE BOOL "FORGE Jolt configuration" FORCE)
endforeach()
set(CPP_RTTI_ENABLED ON CACHE BOOL "Compatible type information for UBSan boundary checks" FORCE)
set(DOUBLE_PRECISION ON CACHE BOOL "FORGE double world positions" FORCE)
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
 set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Match FORGE shared CRT" FORCE)
else()
 set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON CACHE BOOL "Match FORGE static CRT" FORCE)
endif()
FetchContent_Declare(jolt GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
 GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a SOURCE_SUBDIR Build) # 5.6.0
FetchContent_MakeAvailable(jolt)

# miniaudio implementation is private to forge.audio. No headers installed in SDK.
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git
 GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d SOURCE_SUBDIR forge-unused) # 0.11.25
FetchContent_MakeAvailable(miniaudio)
add_library(forge_miniaudio STATIC "${miniaudio_SOURCE_DIR}/miniaudio.c")
target_include_directories(forge_miniaudio PUBLIC "${miniaudio_SOURCE_DIR}")
target_compile_definitions(forge_miniaudio PUBLIC MA_NO_MP3 MA_NO_FLAC MA_NO_ENCODING MA_NO_GENERATION
 MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_WASAPI MA_ENABLE_PULSEAUDIO MA_ENABLE_ALSA MA_ENABLE_NULL)
find_package(Threads REQUIRED)
target_link_libraries(forge_miniaudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
 target_link_libraries(forge_miniaudio PRIVATE m)
endif()
