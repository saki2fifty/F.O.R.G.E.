# UI presentation is opt-in on headless builds. Runtime/model code never links RmlUi.
set(_forge_ui_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
foreach(dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
 set(FT_DISABLE_${dependency} ON CACHE BOOL "Bounded FORGE font backend" FORCE)
endforeach()
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "Font diagnostics" FORCE)
FetchContent_Declare(freetype GIT_REPOSITORY https://github.com/freetype/freetype.git
 GIT_TAG 0a0221a1347e2f1e07c395263540026e9a0aa7c7) # 2.14.3
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
 add_library(Freetype::Freetype ALIAS freetype)
endif()
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
foreach(feature SAMPLES LUA_BINDINGS LOTTIE_PLUGIN SVG_PLUGIN HARFBUZZ_SAMPLE PRECOMPILED_HEADERS)
 set(RMLUI_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(rmlui GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
 GIT_TAG ba95ffe8bfb6370efb2cdcca927eaad4710c5413) # 6.3
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)
set(FREETYPE_VERSION_STRING "2.14.3")
FetchContent_MakeAvailable(rmlui)
unset(CMAKE_DISABLE_FIND_PACKAGE_Freetype)
set(BUILD_SHARED_LIBS "${_forge_ui_shared}")
add_library(forge_ui_presenter src/ui_presenter.cpp)
target_link_libraries(forge_ui_presenter PUBLIC forge_ui_assets forge_ui_protocol PRIVATE RmlUi::Core forge_asset_bytes)
# No SDL/ImGui dependency. The host translates input and supplies a RenderInterface.

if(BUILD_TESTING)
 add_executable(forge_ui_presenter_tests tests/ui_presenter_tests.cpp)
 target_link_libraries(forge_ui_presenter_tests PRIVATE forge_ui_presenter RmlUi::Core)
 add_test(NAME ui_presenter COMMAND forge_ui_presenter_tests ${CMAKE_BINARY_DIR}/ui-presenter-data "${PROJECT_SOURCE_DIR}/resources/ui/LatoLatin-Regular.ttf")
endif()
