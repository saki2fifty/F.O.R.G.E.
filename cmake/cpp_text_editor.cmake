include_guard(GLOBAL)
include(cmake/imgui.cmake)
# Official upstream has no release tags; immutable editor-only snapshot, see dependencies.md.
FetchContent_Declare(cpp_text_editor GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git GIT_TAG ca2f9f1462e3b60e56351bc466acda448c5ea50d)
FetchContent_MakeAvailable(cpp_text_editor)
# Preserve the pin and attribution; repair tab insertion coordinates in a checked
# build-tree copy. Otherwise Paste records an end column that truncates Undo.
set(_forge_widget_source "${cpp_text_editor_SOURCE_DIR}/TextEditor.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_forge_widget_source}")
file(READ "${_forge_widget_source}" _forge_widget_text)
string(REPLACE "\r\n" "\n" _forge_widget_text "${_forge_widget_text}")
string(SHA256 _forge_widget_hash "${_forge_widget_text}")
if(NOT _forge_widget_hash STREQUAL "5a562808f3b32be5361c6241951da7f3e29989260e2f36bdce78d83e79a5d2b0")
 message(FATAL_ERROR "C++ text widget source changed; review/remove the tab-coordinate repair")
endif()
string(REPLACE "\t\t\t++aWhere.mColumn;"
 "\t\t\tif (line[cindex - 1].mChar == '\\t')\n\t\t\t\taWhere.mColumn += mTabSize - aWhere.mColumn % mTabSize;\n\t\t\telse\n\t\t\t\t++aWhere.mColumn;"
 _forge_widget_text "${_forge_widget_text}")
set(_forge_widget_fixed "${CMAKE_BINARY_DIR}/forge-cpp-text-editor/TextEditor.cpp")
file(CONFIGURE OUTPUT "${_forge_widget_fixed}" CONTENT "${_forge_widget_text}" @ONLY NEWLINE_STYLE LF)
file(SHA256 "${_forge_widget_fixed}" _forge_widget_compiled_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _forge_widget_repair_hash)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/forge-cpp-text-editor/source.json" CONTENT
 "{\n  \"upstream_commit\": \"ca2f9f1462e3b60e56351bc466acda448c5ea50d\",\n  \"upstream_lf_sha256\": \"${_forge_widget_hash}\",\n  \"repair_sha256\": \"${_forge_widget_repair_hash}\",\n  \"compiled_sha256\": \"${_forge_widget_compiled_hash}\"\n}\n" @ONLY)
add_library(forge_cpp_text_editor STATIC "${_forge_widget_fixed}")
target_include_directories(forge_cpp_text_editor PUBLIC ${cpp_text_editor_SOURCE_DIR})
target_link_libraries(forge_cpp_text_editor PUBLIC imgui)
# Upstream header uses uint8_t without declaring it. Supply the standard header, no source patch.
if(MSVC)
 target_compile_options(forge_cpp_text_editor PRIVATE /FIcstdint)
else()
 target_compile_options(forge_cpp_text_editor PRIVATE -include cstdint)
endif()
install(FILES ${cpp_text_editor_SOURCE_DIR}/LICENSE DESTINATION licenses COMPONENT Runtime RENAME ImGuiColorTextEdit.txt)
