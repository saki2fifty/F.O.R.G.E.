#pragma once
#include "fbx_profile.hpp"
#include <forge/gltf_source.hpp>
#include <forge/subasset_identity.hpp>
#include <map>

namespace forge::asset_detail {
// Native parsing occurs only in the supervised worker. This description contains
// source-relative image locators, never permission to open a parser-supplied path.
nlohmann::json inspect_fbx(std::span<const std::byte> source, std::stop_token stop = {});
struct FbxInterchange {
    // Generated, standards-shaped private interchange. It is not the authored
    // FBX source, does not allocate AssetIds and is never written into Content.
    GltfSourceBundle generated;
    std::map<std::string, SubassetEvidence> evidence;
};
FbxInterchange convert_fbx_interchange(
    std::span<const std::byte> source,
    const std::map<std::string, std::shared_ptr<const std::vector<std::byte>>>& images,
    const nlohmann::json& settings, std::stop_token stop = {});
} // namespace forge::asset_detail
