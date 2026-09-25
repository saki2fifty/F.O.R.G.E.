if(NOT FORGE_BUILD_ASSET_TOOLS)
 message(FATAL_ERROR "The editor requires FORGE_BUILD_ASSET_TOOLS for its import workflows")
endif()
include(cmake/sdl.cmake)
FetchContent_Declare(imgui_source GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG b48d1afbe8ee8b238e2961dc363a949dd7304e23) # v1.92.9b-docking
FetchContent_MakeAvailable(imgui_source)
add_library(imgui STATIC ${imgui_source_SOURCE_DIR}/imgui.cpp ${imgui_source_SOURCE_DIR}/imgui_draw.cpp ${imgui_source_SOURCE_DIR}/imgui_tables.cpp ${imgui_source_SOURCE_DIR}/imgui_widgets.cpp ${imgui_source_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp ${imgui_source_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_source_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${imgui_source_SOURCE_DIR})
target_link_libraries(imgui PRIVATE SDL3::SDL3)
set(DILIGENT_DEAR_IMGUI_PATH "${imgui_source_SOURCE_DIR}" CACHE PATH "" FORCE)
include(cmake/presentation.cmake)
add_executable(forge_shader_build_worker src/shader_worker_main.cpp)
set_target_properties(forge_shader_build_worker PROPERTIES OUTPUT_NAME forge_shader_build)
target_compile_definitions(forge_shader_build_worker PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(forge_shader_build_worker PRIVATE forge_shader_pipeline forge_import_process forge_shader_diligent Diligent-GraphicsEngineD3D12-shared Diligent-BuildSettings d3d12 dxgi)
copy_required_dlls(forge_shader_build_worker)
add_executable(forge_editor src/editor/main.cpp src/editor/viewport.cpp src/editor/play_presentation.cpp)
add_dependencies(forge_editor forge_shader_build_worker)
target_include_directories(forge_editor PRIVATE src/editor "${diligent_SOURCE_DIR}/DiligentCore")
target_compile_definitions(forge_editor PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(forge_editor PRIVATE forge_presentation_diligent forge_navigation_build forge_navigation_admission forge_animation_conversion forge_authoring SDL3::SDL3 imgui Diligent-Imgui Diligent-GraphicsEngineD3D12-shared Diligent-BuildSettings)
target_link_libraries(forge_editor PRIVATE forge_cache_maintenance forge_shader_authoring forge_shader_diligent forge_material_authoring forge_collision_authoring forge_physics forge_asset_files forge_authored_inspection forge_ui_asset_catalog forge_runtime_dependencies forge_ui_inspection)
add_dependencies(forge_editor forge_ui_inspect)
copy_required_dlls(forge_editor)
add_custom_command(TARGET forge_editor POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:forge_editor>/sdk/include/forge" "$<TARGET_FILE_DIR:forge_editor>/sdk/samples/native"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/include/forge/module_api.h" "$<TARGET_FILE_DIR:forge_editor>/sdk/include/forge/"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/samples/native/movement.c" "${PROJECT_SOURCE_DIR}/samples/native/CMakeLists.txt" "$<TARGET_FILE_DIR:forge_editor>/sdk/samples/native/")

target_link_libraries(forge_editor PRIVATE forge_game_export)
# forge_game_platform is required because PlaySession::start()
# references game_user_data_base() unconditionally; the editor and
# its editor-side tests therefore need FORGE_BUILD_GAME_PLATFORM=ON.
# The SDL-free runtime option (forge_runtime, forge_sdk_play_runtime)
# remains valid because those targets do not include
# src/editor/play.hpp.
if(NOT FORGE_BUILD_GAME_PLATFORM)
  message(FATAL_ERROR "forge_editor requires FORGE_BUILD_GAME_PLATFORM=ON (PlaySession::start references game_user_data_base)")
endif()
target_link_libraries(forge_editor PRIVATE forge_game_platform)
if(BUILD_TESTING)
 add_executable(forge_shader_worker_tests tests/shader_worker_tests.cpp)
 target_include_directories(forge_shader_worker_tests PRIVATE src)
 target_compile_definitions(forge_shader_worker_tests PRIVATE UNICODE _UNICODE NOMINMAX)
 target_link_libraries(forge_shader_worker_tests PRIVATE forge_runtime_package forge_shader_authoring forge_shader_diligent forge_shader_resources forge_asset_bytes)
 add_dependencies(forge_shader_worker_tests forge_shader_build_worker)
 copy_required_dlls(forge_shader_worker_tests)
 add_test(NAME shader_worker COMMAND forge_shader_worker_tests $<TARGET_FILE:forge_shader_build_worker> "${CMAKE_BINARY_DIR}/shader-worker-tests")
 set_tests_properties(shader_worker PROPERTIES TIMEOUT 180)
 add_executable(forge_shader_editor_tests tests/shader_editor_tests.cpp)
 target_include_directories(forge_shader_editor_tests PRIVATE src src/editor)
 target_compile_definitions(forge_shader_editor_tests PRIVATE UNICODE _UNICODE NOMINMAX)
 target_link_libraries(forge_shader_editor_tests PRIVATE forge_shader_authoring forge_shader_diligent imgui SDL3::SDL3)
 add_dependencies(forge_shader_editor_tests forge_shader_build_worker)
 copy_required_dlls(forge_shader_editor_tests)
 add_test(NAME shader_editor COMMAND forge_shader_editor_tests $<TARGET_FILE:forge_shader_build_worker> "${CMAKE_BINARY_DIR}/shader-editor-tests")
 set_tests_properties(shader_editor PROPERTIES TIMEOUT 120)
 add_executable(forge_content_files_tests tests/content_files_tests.cpp)
 target_include_directories(forge_content_files_tests PRIVATE src src/editor)
 target_link_libraries(forge_content_files_tests PRIVATE forge_asset_files imgui SDL3::SDL3)
 copy_required_dlls(forge_content_files_tests)
 add_test(NAME content_files COMMAND forge_content_files_tests "${CMAKE_BINARY_DIR}/content-files-tests")
 set_tests_properties(content_files PROPERTIES TIMEOUT 60)
 add_executable(forge_material_editor_tests tests/material_editor_tests.cpp)
 target_include_directories(forge_material_editor_tests PRIVATE src src/editor)
 target_compile_definitions(forge_material_editor_tests PRIVATE UNICODE _UNICODE NOMINMAX)
 target_link_libraries(forge_material_editor_tests PRIVATE forge_material_authoring forge_texture_authoring imgui SDL3::SDL3)
 copy_required_dlls(forge_material_editor_tests)
 add_test(NAME material_editor COMMAND forge_material_editor_tests "${CMAKE_BINARY_DIR}/material-editor-tests")
 set_tests_properties(material_editor PROPERTIES TIMEOUT 60)
 add_executable(forge_viewport_tests tests/viewport_render_tests.cpp src/editor/viewport.cpp)
 target_include_directories(forge_viewport_tests PRIVATE src src/editor "${diligent_SOURCE_DIR}/DiligentCore")
 target_compile_definitions(forge_viewport_tests PRIVATE UNICODE _UNICODE NOMINMAX FORGE_TEST_FX_SOURCE="${diligent_SOURCE_DIR}/DiligentFX")
 target_link_libraries(forge_viewport_tests PRIVATE forge_authoring imgui Diligent-Imgui Diligent-GraphicsEngineD3D12-shared Diligent-BuildSettings d3d12 dxgi d3dcompiler)
 copy_required_dlls(forge_viewport_tests)
 target_link_libraries(forge_viewport_tests PRIVATE forge_shader_diligent forge_presentation_diligent)
 add_test(NAME editor_viewport_render COMMAND forge_viewport_tests "${CMAKE_BINARY_DIR}/grid-test-images")
 # Combined device-lifetime coverage already took 56-57s before the additional
 # volume-exit cases. Keep its assertions and individual case bounds; allow
 # headroom for the complete shader/readback workload on hosted WARP.
 set_tests_properties(editor_viewport_render PROPERTIES TIMEOUT 120)
 foreach(render_case IN ITEMS morph skin frame optics)
  add_test(NAME editor_render_${render_case} COMMAND forge_viewport_tests "${CMAKE_BINARY_DIR}/grid-test-images/${render_case}" ${render_case})
  set_tests_properties(editor_render_${render_case} PROPERTIES TIMEOUT 60)
 endforeach()
 add_executable(forge_editor_native_tests tests/editor_native_tests.cpp)
 target_include_directories(forge_editor_native_tests PRIVATE src/editor)
 target_link_libraries(forge_editor_native_tests PRIVATE forge_authoring SDL3::SDL3 forge_game_platform)
 add_test(NAME editor_native_iteration COMMAND forge_editor_native_tests "${PROJECT_SOURCE_DIR}" $<TARGET_FILE:forge_runtime> "${CMAKE_COMMAND}" "${CMAKE_MAKE_PROGRAM}")
 set_tests_properties(editor_native_iteration PROPERTIES TIMEOUT 240)
 add_executable(forge_fault_runtime tests/fault_runtime.cpp)
 target_link_libraries(forge_fault_runtime PRIVATE nlohmann_json::nlohmann_json)
 add_executable(forge_editor_tests tests/editor_tests.cpp)
 target_include_directories(forge_editor_tests PRIVATE src/editor)
 target_link_libraries(forge_editor_tests PRIVATE forge_navigation_build forge_navigation_admission forge_animation_conversion forge_authoring SDL3::SDL3 imgui forge_game_platform)
 target_link_libraries(forge_editor_tests PRIVATE forge_render_bounds)
 add_test(NAME editor_process_and_scale COMMAND forge_editor_tests $<TARGET_FILE:forge_runtime> $<TARGET_FILE:forge_fault_runtime>)
 set_tests_properties(editor_process_and_scale PROPERTIES TIMEOUT 45)
endif()

target_link_libraries(forge_editor PRIVATE forge_ui_diligent)
add_custom_command(TARGET forge_editor POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/resources/ui" "$<TARGET_FILE_DIR:forge_editor>/resources/ui")

# Reuse upstream SDL3 key/modifier and IME translation only; FORGE owns routing.
target_sources(forge_editor PRIVATE "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
target_include_directories(forge_editor PRIVATE "${rmlui_SOURCE_DIR}/Backends")
target_compile_definitions(forge_editor PRIVATE RMLUI_SDL_VERSION_MAJOR=3)
target_link_libraries(forge_editor PRIVATE RmlUi::Core)

if(BUILD_TESTING)
 add_executable(forge_authored_components_editor_tests tests/authored_components_editor_tests.cpp)
 target_include_directories(forge_authored_components_editor_tests PRIVATE src/editor)
 target_link_libraries(forge_authored_components_editor_tests PRIVATE forge_authoring forge_authored_inspection SDL3::SDL3 imgui)
 add_dependencies(forge_authored_components_editor_tests forge_schema_worker_fixture)
 add_test(NAME authored_components_editor COMMAND forge_authored_components_editor_tests $<TARGET_FILE:forge_schema_worker_fixture>)
 set_tests_properties(authored_components_editor PROPERTIES TIMEOUT 45)
 add_executable(forge_ui_render_tests tests/ui_render_tests.cpp)
 target_include_directories(forge_ui_render_tests PRIVATE "${diligent_SOURCE_DIR}/DiligentCore")
 target_compile_definitions(forge_ui_render_tests PRIVATE UNICODE _UNICODE NOMINMAX)
 target_link_libraries(forge_ui_render_tests PRIVATE forge_ui_diligent RmlUi::Core Diligent-GraphicsEngineD3D12-shared Diligent-BuildSettings d3d12 dxgi)
 copy_required_dlls(forge_ui_render_tests)
 add_test(NAME runtime_ui_render COMMAND forge_ui_render_tests "${CMAKE_BINARY_DIR}/ui-test-images" "${PROJECT_SOURCE_DIR}/resources/ui/LatoLatin-Regular.ttf")
endif()
if(BUILD_TESTING)
 add_executable(forge_ui_input_tests tests/ui_input_tests.cpp "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
 target_include_directories(forge_ui_input_tests PRIVATE src/editor "${rmlui_SOURCE_DIR}/Backends")
 target_compile_definitions(forge_ui_input_tests PRIVATE RMLUI_SDL_VERSION_MAJOR=3)
 target_link_libraries(forge_ui_input_tests PRIVATE forge_ui_presenter forge_authoring RmlUi::Core SDL3::SDL3 imgui forge_game_platform)
 add_test(NAME runtime_ui_input COMMAND forge_ui_input_tests $<TARGET_FILE:forge_runtime> "${PROJECT_SOURCE_DIR}/resources/ui/LatoLatin-Regular.ttf" "${CMAKE_BINARY_DIR}/ui-input-data")
endif()

# Durable shim for the SdlGameCursor / cursor-correction review. The
# stub is compiled directly into the test executable so the stub's
# SDL_SetWindowRelativeMouseMode / SDL_GetWindowRelativeMouseMode /
# SDL_GetWindowFlags / SDL_GetError / SDL_GetWindowID / SDL_LogWarn
# symbols provide the SDL surface the cursor class calls. No real SDL3
# runtime is linked: this test does not own the SDL runtime, only the
# symbols the cursor contract depends on. Header-only target
# SDL3::Headers supplies the SDL3 include path without leaking the
# editor-side native-headers location into the product tree.
if(BUILD_TESTING)
 add_executable(forge_cursor_truthful_tests
  tests/cursor_truthful_tests.cpp
  tests/sdl_cursor_stub.cpp)
 target_include_directories(forge_cursor_truthful_tests PRIVATE src)
 target_link_libraries(forge_cursor_truthful_tests PRIVATE SDL3::Headers)
 add_test(NAME cursor_truthful COMMAND forge_cursor_truthful_tests)
endif()

# SDK platform-effects adapter focus-gate coverage is owned by the
# native fixture (tests/editor_sdk_workflow.hpp stage 2 surrender
# substages) and asserted by tests/editor_sdk_package_test.py as a
# REQUIRED acceptance check (focus_gated_routing=true plus the
# sdk-outside-surrender / sdk-outside-regain captures). The fixture
# drives the real forge::SdkPlatformEffects pump() against a real
# running PlaySession, which is the only way to reach the
# acquire_routing predicate that requires play.ready()==true.
# No headless adapter test target is needed; adding one would
# duplicate the predicate path with a non-running PlaySession that
# never satisfies play.ready() and therefore cannot fail with the
# pre-fix focus predicate — that is a "passes by construction" case
# the previous batch incorrectly claimed as regression coverage.

# Render the actual editor UI on WARP in a disposable project/preferences directory.
if(BUILD_TESTING)
 add_executable(forge_editor_fixture src/editor/main.cpp src/editor/viewport.cpp src/editor/play_presentation.cpp)
 get_target_property(editor_links forge_editor LINK_LIBRARIES)
 get_target_property(editor_includes forge_editor INCLUDE_DIRECTORIES)
 get_target_property(editor_definitions forge_editor COMPILE_DEFINITIONS)
 target_include_directories(forge_editor_fixture PRIVATE tests ${editor_includes})
 target_compile_definitions(forge_editor_fixture PRIVATE ${editor_definitions} FORGE_UI_FIXTURE=1)
 target_link_libraries(forge_editor_fixture PRIVATE ${editor_links} d3d12 dxgi)
 target_sources(forge_editor_fixture PRIVATE "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
 # Fixture inherits forge_editor's transitive forge_game_platform
 # link; forge_editor already FATAL_ERRORs under FORGE_BUILD_GAME_PLATFORM=OFF.
 add_dependencies(forge_editor_fixture forge_editor forge_runtime forge_schema_worker_fixture)
 copy_required_dlls(forge_editor_fixture)
 add_test(NAME editor_redesign_render COMMAND forge_editor_fixture "${CMAKE_BINARY_DIR}/grid-test-images/editor")
 set_tests_properties(editor_redesign_render PROPERTIES TIMEOUT 260)
 add_test(NAME editor_input_workflow COMMAND forge_editor_fixture "${CMAKE_BINARY_DIR}/grid-test-images/editor-workflow" --workflow)
 set_tests_properties(editor_input_workflow PROPERTIES TIMEOUT 180)
endif()
