#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "fbx_importer.hpp"
#include "fbx_native.hpp"
#include "model_bundle.hpp"
#include <set>
namespace forge::asset_detail {
std::vector<ArtifactFile> execute_fbx_recipe(ImportProcessRequest request, std::stop_token stop) {
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto& payload = request.payload;
    require(!stop.stop_requested(), "FBX preparation cancelled");
    require(payload.at("recipe") == fbx_importer_id &&
                payload.at("revision") == fbx_recipe_revision(),
            "FBX worker recipe/toolchain revision mismatch");
    std::map<std::string, std::shared_ptr<const std::vector<std::byte>>> files;
    for (auto& file : request.inputs)
        require(files
                    .emplace(file.name,
                             std::make_shared<const std::vector<std::byte>>(std::move(file.bytes)))
                    .second,
                "Duplicate FBX snapshot file");
    const auto source = files.find("source.fbx");
    require(source != files.end(), "FBX snapshot omitted its source");
    const auto digest = content_digest(*source->second);
    require(payload.at("source_digest") == digest, "FBX source snapshot digest mismatch");
    const auto phase = payload.at("phase").get<std::string>();
    if (phase == "inspect") {
        require(files.size() == 1, "FBX inspection accepts only the captured source");
        const auto text = inspect_fbx(*source->second, stop).dump();
        require(text.size() <= 4 * 1024 * 1024, "FBX inspection output exceeds bounds");
        const auto bytes = std::as_bytes(std::span(text));
        return {{"inspection.json", {bytes.begin(), bytes.end()}}};
    }
    require(phase == "cook", "Unsupported FBX worker phase");
    const auto manifest = files.find("images.json");
    require(manifest != files.end(), "FBX snapshot omitted its image description");
    const auto images = parse_bounded_json(*manifest->second, 4 * 1024 * 1024);
    require(images.is_array() && images.size() <= 4096, "FBX snapshot image count exceeds bounds");
    std::set<std::string> used{"source.fbx", "images.json"};
    std::map<std::string, std::shared_ptr<const std::vector<std::byte>>> captured;
    for (const auto& image : images) {
        const auto name = image.at("file").get<std::string>(),
                   key = image.at("key").get<std::string>();
        const auto at = files.find(name);
        require(name.starts_with("image-") && name.ends_with(".bin") &&
                    key.starts_with("fbx:image:") && at != files.end() &&
                    image.at("sha256") == content_digest(*at->second) &&
                    captured.emplace(key, at->second).second,
                "FBX captured image is missing, duplicated or changed");
        used.insert(name);
    }
    require(used.size() == files.size(), "Unexpected FBX snapshot files");
    const auto settings = model_settings(fbx_importer_id)
                              .effective(payload.at("settings").get<ImportSettingsDocument>());
    auto interchange = convert_fbx_interchange(*source->second, captured, settings, stop);
    const auto generated_digest = interchange.generated.source_digest;
    auto output = cook_model_interchange(std::move(interchange.generated), settings,
                                         payload.at("backend").get<std::string>(), stop);
    bool found = false;
    for (auto& file : output) {
        if (file.name == "rig-plan.json") {
            auto plan = parse_bounded_json(file.bytes, 16 * 1024 * 1024);
            plan["source_key"] = interchange.evidence.at("/rig/skeleton").exporter_key;
            for (std::size_t i = 0; i < plan.at("clips").size(); ++i)
                plan["clips"][i]["source_key"] =
                    interchange.evidence.at("/animations/" + std::to_string(i)).exporter_key;
            const auto text = plan.dump();
            const auto bytes = std::as_bytes(std::span(text));
            file.bytes.assign(bytes.begin(), bytes.end());
        }
        if (file.name != "model.json")
            continue;
        require(!found, "FBX model cook returned duplicate index");
        found = true;
        auto index = decode_model_bundle_index(file.bytes);
        index.source_digest = digest;
        index.evidence_schema = fbx_evidence_schema;
        index.hierarchy["fbx"] = {{"version", 1},
                                  {"parser_revision", fbx_parser_revision},
                                  {"interchange_sha256", generated_digest}};
        for (auto& member : index.members) {
            const auto evidence = interchange.evidence.find(member.identity.address);
            require(evidence != interchange.evidence.end(),
                    "FBX cook lost source UID correspondence");
            member.identity.evidence.exporter_key = evidence->second.exporter_key;
        }
        file.bytes = encode_model_bundle_index(index);
    }
    require(found, "FBX model cook omitted index");
    return output;
}
} // namespace forge::asset_detail
