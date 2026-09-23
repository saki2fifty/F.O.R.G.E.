include(cmake/sdl.cmake)
include(cmake/presentation.cmake)
add_library(forge_game_presentation src/game_presentation.cpp)
target_compile_definitions(forge_game_presentation PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(forge_game_presentation PUBLIC forge_simulation forge_presentation_diligent forge_ui_diligent)
add_executable(forge_game src/game_main.cpp src/game_device_d3d12.cpp
 "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
target_include_directories(forge_game PRIVATE src "${rmlui_SOURCE_DIR}/Backends")
target_compile_definitions(forge_game PRIVATE UNICODE _UNICODE NOMINMAX RMLUI_SDL_VERSION_MAJOR=3)
target_link_libraries(forge_game PRIVATE forge_game_content forge_game_presentation
 forge_game_platform forge_game_storage SDL3::SDL3 RmlUi::Core
 Diligent-GraphicsEngineD3D12-shared Diligent-BuildSettings d3d12 dxgi)
copy_required_dlls(forge_game)
add_custom_command(TARGET forge_game POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/resources/ui" "$<TARGET_FILE_DIR:forge_game>/resources/ui")
if(BUILD_TESTING)
 add_executable(forge_game_fixture src/game_main.cpp src/game_device_d3d12.cpp
  "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
 get_target_property(game_links forge_game LINK_LIBRARIES)
 get_target_property(game_includes forge_game INCLUDE_DIRECTORIES)
 get_target_property(game_definitions forge_game COMPILE_DEFINITIONS)
 target_include_directories(forge_game_fixture PRIVATE tests ${game_includes})
 target_compile_definitions(forge_game_fixture PRIVATE ${game_definitions} FORGE_GAME_FIXTURE=1)
 target_link_libraries(forge_game_fixture PRIVATE ${game_links})
 add_dependencies(forge_game_fixture forge_game)
 copy_required_dlls(forge_game_fixture)
 add_test(NAME standalone_game_workflow COMMAND forge_game_fixture "${CMAKE_BINARY_DIR}/grid-test-images/game")
 set_tests_properties(standalone_game_workflow PROPERTIES TIMEOUT 120)
endif()
