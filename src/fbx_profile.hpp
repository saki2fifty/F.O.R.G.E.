#pragma once
#include <forge/asset_build.hpp>
namespace forge::asset_detail {
inline constexpr const char* fbx_importer_id = "forge.model.fbx";
inline constexpr const char* fbx_parser_revision = "26a482ae66871d7de36eb722aa060bce95bce274";
inline constexpr const char* fbx_evidence_schema = "forge.fbx-model.v1";
inline std::string fbx_parser_tool_revision() {
    return asset_build_digest(
        {{"commit", fbx_parser_revision},
         {"source_sha256", "4a956c26a708e40d82ecb0aeaee54da709d5ba7c144c9642174970059bda7e1d"},
         {"header_sha256", "f787be529af577efc04f3e3e735844a08556718dbb613d1211c2b313d72895dc"}});
}
} // namespace forge::asset_detail
