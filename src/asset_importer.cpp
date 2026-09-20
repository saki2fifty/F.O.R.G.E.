#include <algorithm>
#include <forge/asset_importer.hpp>
#include <set>

namespace forge {
namespace {
bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '.' || c == '-';
           });
}
void identifiers(const std::vector<std::string>& values) {
    if (values.empty() || values.size() > 256)
        throw std::runtime_error("Importer declaration needs bounded supported identifiers");
    std::set<std::string> unique;
    for (const auto& value : values)
        if (!identifier(value) || !unique.insert(value).second)
            throw std::runtime_error("Invalid/duplicate importer declaration identifier");
}
bool target_matches(const ImportTarget& declared, const ImportTarget& requested) {
    return (declared.platform == "*" || declared.platform == requested.platform) &&
           (declared.backend == "*" || declared.backend == requested.backend) &&
           (declared.profile == "*" || declared.profile == requested.profile);
}
void target_valid(const ImportTarget& target, bool wildcards) {
    for (const auto* value : {&target.platform, &target.backend, &target.profile})
        if (!identifier(*value) && !(wildcards && *value == "*"))
            throw std::runtime_error("Invalid import target platform/backend/profile");
}
std::string extension(const std::filesystem::path& path) {
    auto value = path.extension().string();
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return value;
}
} // namespace
void AssetImporterRegistry::add(std::shared_ptr<const AssetImporter> importer) {
    if (sealed_ || !importer || entries_.size() >= 256)
        throw std::runtime_error("Importer registry is sealed, full or registration is null");
    const auto& info = importer->descriptor();
    if (!identifier(info.id) || !identifier(info.revision) || info.label.empty() ||
        info.label.size() > 256 || info.description.empty() || info.description.size() > 4096 ||
        !identifier(info.output_format) || !info.output_version || !info.diagnostic_version ||
        info.extensions.empty() || info.extensions.size() > 128 || info.targets.empty() ||
        info.targets.size() > 256 || info.id != importer->settings().importer())
        throw std::runtime_error("Invalid importer identity/metadata/settings declaration");
    identifiers(info.source_kinds);
    identifiers(info.output_types);
    std::set<std::string> extensions;
    for (const auto& ext : info.extensions)
        if (ext.size() < 2 || ext.size() > 32 || ext[0] != '.' ||
            !identifier(std::string_view(ext).substr(1)) ||
            std::any_of(ext.begin(), ext.end(), [](char c) { return c >= 'A' && c <= 'Z'; }) ||
            !extensions.insert(ext).second)
            throw std::runtime_error(
                "Importer extensions must be unique lowercase dotted suffixes");
    std::set<ImportTarget> targets;
    for (const auto& target : info.targets) {
        target_valid(target, true);
        if (!targets.insert(target).second)
            throw std::runtime_error("Duplicate importer target declaration");
    }
    const auto& limits = info.limits;
    if (limits.memory_bytes < 16 * 1024 * 1024 ||
        limits.memory_bytes > 16ull * 1024 * 1024 * 1024 || !limits.output_bytes ||
        limits.output_bytes > 8ull * 1024 * 1024 * 1024 || !limits.seconds ||
        limits.seconds > 3600 || !limits.output_files || limits.output_files > 65536 ||
        (info.execution != ImportExecution::IsolatedProcess &&
         info.execution != ImportExecution::TrustedCpuTask))
        throw std::runtime_error("Importer worker limits/execution policy are invalid");
    const auto id = info.id;
    if (!entries_.emplace(id, std::move(importer)).second)
        throw std::runtime_error("Duplicate importer stable identity: " + id);
}
void AssetImporterRegistry::seal() { sealed_ = true; }
std::vector<AssetImporterDescriptor> AssetImporterRegistry::descriptors() const {
    std::vector<AssetImporterDescriptor> result;
    for (const auto& [id, importer] : entries_) {
        (void)id;
        result.push_back(importer->descriptor());
    }
    return result;
}
std::shared_ptr<const AssetImporter> AssetImporterRegistry::find(std::string_view id) const {
    const auto found = entries_.find(std::string(id));
    return found == entries_.end() ? nullptr : found->second;
}
std::vector<ImporterCandidate> AssetImporterRegistry::candidates(const ImportProbe& source,
                                                                 const ImportTarget& target) const {
    if (!sealed_)
        throw std::runtime_error("Seal importer registrations before probing sources");
    target_valid(target, false);
    if (source.prefix.size() > 64 * 1024 || source.source.empty() ||
        source.source.generic_string().size() > 4096)
        throw std::runtime_error("Importer probe path/prefix exceeds admission limits");
    const auto ext = extension(source.source);
    std::vector<ImporterCandidate> result;
    for (const auto& [id, importer] : entries_) {
        (void)id;
        const auto& descriptor = importer->descriptor();
        if (std::find(descriptor.extensions.begin(), descriptor.extensions.end(), ext) ==
                descriptor.extensions.end() ||
            std::none_of(descriptor.targets.begin(), descriptor.targets.end(),
                         [&](const auto& value) { return target_matches(value, target); }))
            continue;
        auto match = importer->probe(source);
        if (match.match == ImportProbeMatch::No)
            continue;
        if ((match.match != ImportProbeMatch::Possible &&
             match.match != ImportProbeMatch::Strong) ||
            match.reason.size() > 4096 ||
            std::find(descriptor.source_kinds.begin(), descriptor.source_kinds.end(),
                      match.source_kind) == descriptor.source_kinds.end())
            throw std::runtime_error("Importer returned an invalid source probe: " + descriptor.id);
        result.push_back({importer, std::move(match)});
    }
    return result;
}
std::shared_ptr<const AssetImporter>
AssetImporterRegistry::select(const ImportProbe& source, const ImportTarget& target,
                              std::optional<std::string_view> explicit_importer) const {
    const auto matches = candidates(source, target);
    if (explicit_importer) {
        const auto found = std::find_if(matches.begin(), matches.end(), [&](const auto& candidate) {
            return candidate.importer->descriptor().id == *explicit_importer;
        });
        if (found == matches.end())
            throw std::runtime_error(
                "Selected importer is unavailable or does not support this source/target");
        return found->importer;
    }
    if (matches.empty())
        throw std::runtime_error("No registered importer supports this source/target");
    if (matches.size() != 1)
        throw std::runtime_error("Multiple importers match; choose an importer explicitly");
    return matches.front().importer;
}
} // namespace forge
