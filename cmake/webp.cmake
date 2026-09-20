# Official stable libwebp 1.6.0, including its pinned native RIFF demuxer.
foreach(option WEBP_BUILD_ANIM_UTILS WEBP_BUILD_CWEBP WEBP_BUILD_DWEBP
 WEBP_BUILD_GIF2WEBP WEBP_BUILD_IMG2WEBP WEBP_BUILD_VWEBP WEBP_BUILD_WEBPINFO
 WEBP_BUILD_LIBWEBPMUX WEBP_BUILD_WEBPMUX WEBP_BUILD_EXTRAS WEBP_BUILD_WEBP_JS
 WEBP_BUILD_FUZZTEST WEBP_USE_THREAD)
 set(${option} OFF CACHE BOOL "Private FORGE image decoder" FORCE)
endforeach()
set(WEBP_ENABLE_SIMD ON CACHE BOOL "Native runtime-dispatched image decoder" FORCE)
set(WEBP_LINK_STATIC ON CACHE BOOL "Private static codec" FORCE)
set(_forge_shared_before_webp "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(webp
 URL https://codeload.github.com/webmproject/libwebp/tar.gz/4fa21912338357f89e4fd51cf2368325b59e9bd9
 URL_HASH SHA256=923f3382a47a2af185c3240c954cf004428b237bd7317413a95146d01eb4b94b
 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(webp)
set(BUILD_SHARED_LIBS "${_forge_shared_before_webp}")
