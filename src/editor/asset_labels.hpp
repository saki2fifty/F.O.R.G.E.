#pragma once
#include <forge/assets.hpp>
namespace forge {
inline std::string content_member_name(const AssetRecord& record) {
    const auto model = record.metadata.find("forge.model");
    if (model != record.metadata.end() && model->is_object()) {
        const auto name = model->find("name");
        if (name != model->end() && name->is_string() &&
            !name->get_ref<const std::string&>().empty())
            return name->get<std::string>();
    }
    const auto clip = record.metadata.find("clip_name");
    if (clip != record.metadata.end() && clip->is_string())
        return clip->get<std::string>();
    return record.subasset ? record.type + " / " + record.subasset->key : std::string{};
}
} // namespace forge
