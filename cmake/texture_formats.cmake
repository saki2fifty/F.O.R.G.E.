include_guard(GLOBAL)
add_library(forge_texture_formats STATIC src/texture_formats.cpp)
target_include_directories(forge_texture_formats PUBLIC src "${diligent_SOURCE_DIR}/DiligentCore")
target_link_libraries(forge_texture_formats PUBLIC forge_texture Diligent-BuildSettings)
