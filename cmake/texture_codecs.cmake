# Official stable KTX Software 4.4.2 and its bundled Basis encoder/transcoder.
# Source admission precedes native parsing. No OpenGL/Vulkan upload or optional
# non-open-source Ericsson ETC decoder. All graphics upload remains Diligent's.
foreach(option KTX_FEATURE_TESTS KTX_FEATURE_TOOLS KTX_FEATURE_TOOLS_CTS KTX_FEATURE_ETC_UNPACK
 KTX_FEATURE_VK_UPLOAD KTX_FEATURE_GL_UPLOAD KTX_FEATURE_DOC KTX_FEATURE_JNI KTX_FEATURE_PY
 BASISU_SUPPORT_OPENCL BASISU_SUPPORT_SSE)
 set(${option} OFF CACHE BOOL "FORGE private texture codecs" FORCE)
endforeach()
set(KTX_FEATURE_KTX1 ON CACHE BOOL "Admitted KTX1 container support" FORCE)
set(KTX_FEATURE_KTX2 ON CACHE BOOL "Admitted KTX2/Basis support" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "No KTX graphics host" FORCE)
set(ASTCENC_ISA_NONE ON CACHE BOOL "Portable scalar codec; no AVX2 requirement" FORCE)
set(_forge_shared_before_ktx "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(ktx
 URL https://codeload.github.com/KhronosGroup/KTX-Software/tar.gz/4d6fc70eaf62ad0558e63e8d97eb9766118327a6
 URL_HASH SHA256=4d0a3c4470c67e0f1544d2a92f379dc919c9627a5c3fa5c4fcaf4c22324827f5
 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ktx)
set(BUILD_SHARED_LIBS "${_forge_shared_before_ktx}")
# Native byte-wise implementation avoids C++ misaligned loads in every profile.
# Upstream's automatic sanitizer detection covers Clang, but misses GCC.
target_compile_definitions(ktx PRIVATE BASISD_USE_UNALIGNED_WORD_READS=0)
add_library(forge_texture_ktx STATIC src/texture_ktx.cpp)
target_include_directories(forge_texture_ktx PRIVATE
 "${ktx_SOURCE_DIR}/lib" "${ktx_SOURCE_DIR}/external/basisu/encoder")
target_compile_definitions(forge_texture_ktx PRIVATE BASISD_SUPPORT_KTX2=1
 BASISD_SUPPORT_KTX2_ZSTD=0 BASISU_SUPPORT_SSE=0 BASISU_SUPPORT_OPENCL=0
 BASISD_USE_UNALIGNED_WORD_READS=0)
target_link_libraries(forge_texture_ktx PUBLIC forge_texture PRIVATE ktx)
if(WIN32)
 target_compile_definitions(forge_texture_ktx PRIVATE NOMINMAX)
endif()
