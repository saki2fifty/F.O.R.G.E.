#include "navigation_behavior_authoring.hpp"
#include "asset_bytes.hpp"
#include "state_graph_layout.hpp"
#include <algorithm>

namespace forge {
std::uint32_t add_navigation_behavior_state(nlohmann::json& source) {
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (id > UINT32_MAX || source.at("states").size() >= 32)
        throw std::runtime_error("Behavior state or identity capacity reached");
    source["states"].push_back({{"id", id},
                                {"name", "State " + std::to_string(id)},
                                {"action", {{"kind", "wait"}}},
                                {"transitions", nlohmann::json::array()}});
    source["next_id"] = id + 1;
    if (source.at("entry") == 0)
        source["entry"] = id;
    return std::uint32_t(id);
}
std::uint32_t connect_navigation_behavior_states(nlohmann::json& source, std::uint32_t from,
                                                 std::uint32_t to) {
    auto& states = source.at("states");
    auto at = std::find_if(states.begin(), states.end(),
                           [&](const auto& s) { return s.at("id") == from; });
    const auto target =
        std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.at("id") == to; });
    std::size_t total = 0;
    for (const auto& s : states)
        total += s.at("transitions").size();
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (at == states.end() || target == states.end())
        throw std::runtime_error("Behavior transition state is missing");
    if (id > UINT32_MAX || at->at("transitions").size() >= 8 || total >= 128)
        throw std::runtime_error("Behavior transition or identity capacity reached");
    (*at)["transitions"].push_back(
        {{"id", id}, {"to", to}, {"condition", {{"kind", "elapsed"}, {"seconds", 1}}}});
    source["next_id"] = id + 1;
    return std::uint32_t(id);
}
void remove_navigation_behavior_state(nlohmann::json& source, std::uint32_t id) {
    auto& states = source.at("states");
    const auto at =
        std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.at("id") == id; });
    if (at == states.end())
        throw std::runtime_error("Behavior state is missing");
    states.erase(at);
    for (auto& s : states) {
        auto& edges = s.at("transitions");
        for (auto e = edges.begin(); e != edges.end();)
            e = e->at("to") == id ? edges.erase(e) : std::next(e);
    }
    asset_detail::remove_state_graph_position(source, id, navigation_behavior_layout_format);
    if (source.at("entry") == id)
        source["entry"] = states.empty() ? nlohmann::json(0) : states.front().at("id");
}
namespace {
constexpr const char* importer_id = "forge.navigation.behavior";
constexpr const char* recipe = "forge.navigation.behavior.v1";
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
struct Capture {
    NavigationBehaviorSource source;
    AssetBuildInput input;
};
Capture capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Navigation behavior import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), navigation_behavior_byte_limit);
    Capture result{NavigationBehaviorSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Navigation behavior source/catalog identity mismatch");
    result.input.source_digest = asset_detail::content_digest(bytes);
    result.input.importer = importer_id;
    result.input.importer_revision = recipe;
    result.input.output_format = "forge.navigation-behavior";
    result.input.output_version = 1;
    result.input.platform = request.target.platform;
    result.input.backend = request.target.backend;
    result.input.profile = request.target.profile;
    return result;
}
const ArtifactFile& artifact_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files.front().name == "behavior.json",
            "Navigation behavior artifact requires one behavior.json");
    (void)NavigationBehaviorSource::parse_cooked(files.front().bytes);
    return files.front();
}
class BehaviorImporter final : public AssetImporter {
  public:
    BehaviorImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = importer_id;
                  d.revision = recipe;
                  d.label = "Navigation Behavior";
                  d.description = "Cook bounded patrol, wait and target-chase state graphs.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.navigation-behavior"};
                  d.output_types = {NavigationBehaviorAsset::type};
                  d.output_format = "forge.navigation-behavior";
                  d.output_version = 1;
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.limits.output_files = 1;
                  d.limits.output_bytes = navigation_behavior_cooked_byte_limit;
                  return d;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& value) const override {
        return path_utf8(value.source).ends_with(".behavior.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.navigation-behavior",
                                       "Navigation behavior source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported navigation behavior target");
        (void)settings().effective(request.settings);
        auto value = capture(request, stop);
        return {std::move(value.input), {}, {{"asset", request.asset}}};
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto value = capture(request, stop);
        require(value.input.document() == plan.input.document(),
                "Navigation behavior source changed after discovery");
        if (progress)
            progress(.5, "Validating navigation behavior graph");
        auto bytes = value.source.cook();
        require(!stop.stop_requested(), "Navigation behavior import cancelled");
        return {{"behavior.json", std::move(bytes)}};
    }
    void validate(const CachedArtifact& value) const override { (void)artifact_file(value.files); }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> navigation_behavior_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<BehaviorImporter>());
    registry->seal();
    return registry;
}
void prepare_navigation_behavior_publication(AssetPublicationCandidate& candidate,
                                             const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Navigation behavior publication identity/family mismatch");
    const auto& file = artifact_file(candidate.files);
    require(NavigationBehaviorSource::parse_cooked(file.bytes).asset() == candidate.ticket.owner,
            "Navigation behavior cooked/publication identity mismatch");
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.navigation-behavior.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          NavigationBehaviorAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.navigation_behavior",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
