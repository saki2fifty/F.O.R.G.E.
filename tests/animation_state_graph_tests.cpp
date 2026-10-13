#include "animation_states_authoring.hpp"
#include "animation_states_layout.hpp"
#include "asset_reference_impact.hpp"
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid state graph edit was admitted");
}
AnimationStatesSource fixture(AssetId id) {
    auto source = AnimationStatesSource::create(id);
    source.document["skeleton"] = AssetId::generate();
    source.document["entry"] = 1;
    source.document["next_id"] = 4;
    for (std::uint32_t state = 1; state <= 3; ++state)
        source.document["states"].push_back({{"id", state},
                                             {"name", "State " + std::to_string(state)},
                                             {"clip", AssetId::generate()},
                                             {"loop", true},
                                             {"speed", 1},
                                             {"transition_seconds", .25}});
    source.document["states"][1]["exit"] = {
        {"state", 3}, {"phase", .5}, {"custom", AssetId::generate()}};
    return source;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need state graph scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        atomic_write(root / "forge.assets.json",
                     Json{{"version", 1}, {"assets", Json::array()}}.dump());
        auto lease = std::make_shared<ProjectLease>(root);
        auto doc = AnimationStatesDocument::create(lease, "Assets/Graph.states.json", fixture);
        const auto original = doc->source().document;
        const auto cooked = doc->source().cook();
        const auto revision = doc->revision();
        doc->edit(revision, "Move state", [](Json& source) {
            asset_detail::set_state_graph_position(source, 2, {20, 40});
        });
        require(doc->dirty() && doc->source().cook() == cooked,
                "Graph position changed cooked animation behavior");
        const auto moved = doc->source().document;
        const std::array references{AssetReferenceDocument{doc->locator(), moved}};
        const Json schema = {{"components", Json::array()}};
        const auto inspected = collect_asset_references(schema, references);
        require(inspected.references.size() == 4 &&
                    std::none_of(inspected.uninspected.begin(), inspected.uninspected.end(),
                                 [](const auto& issue) {
                                     return issue.find("editor_layout") != std::string::npos;
                                 }),
                "Known layout was mistaken for an uninspected asset reference");
        require(asset_detail::state_graph_position(moved, 2, 0) == std::array<double, 2>{20, 40},
                "Graph position lost its stable state binding");
        doc->undo();
        require(doc->source().document == original, "Layout Undo failed");
        doc->redo();
        require(doc->source().document == moved, "Layout Redo failed");
        doc->save();
        require(AnimationStatesDocument(lease, doc->locator()).source().document == moved,
                "Layout reopen lost source data");
        rejects([&] {
            doc->edit(revision, "Stale gesture", [](Json& source) {
                asset_detail::set_state_graph_position(source, 2, {80, 90});
            });
        });
        require(doc->source().document == moved, "Stale gesture changed the document");
        auto invalid = doc->source();
        invalid.document["editor_layout"]["positions"].push_back(
            invalid.document["editor_layout"]["positions"][0]);
        rejects([&] { invalid.validate(); });
        for (const auto value :
             {Json(true), Json(1000001), Json(std::numeric_limits<double>::infinity())}) {
            invalid = doc->source();
            invalid.document["editor_layout"]["positions"][0]["x"] = value;
            rejects([&] { invalid.validate(); });
        }
        invalid = doc->source();
        invalid.document["editor_layout"]["positions"][0]["state"] = 9;
        rejects([&] { invalid.validate(); });
        invalid = doc->source();
        invalid.document["editor_layout"]["positions"][0]["state"] = 4294967296ull;
        rejects([&] { invalid.validate(); });
        doc->edit(doc->revision(), "Rename/reorder", [](Json& source) {
            source["states"][1]["name"] = "Renamed";
            std::reverse(source["states"].begin(), source["states"].end());
        });
        require(asset_detail::state_graph_position(doc->source().document, 2, 1) ==
                    std::array<double, 2>{20, 40},
                "Rename/reorder retargeted graph layout");
        const auto extension = AssetId::generate();
        doc->edit(doc->revision(), "Layout extension", [&](Json& source) {
            source["editor_layout"]["positions"][0]["custom"] = extension;
        });
        doc->edit(doc->revision(), "Move extended state", [](Json& source) {
            asset_detail::set_state_graph_position(source, 2, {50, 60});
        });
        require(doc->source().document["editor_layout"]["positions"][0]["custom"] ==
                    Json(extension),
                "Position edit erased opaque layout data");
        const std::array extended{AssetReferenceDocument{doc->locator(), doc->source().document}};
        const auto impact = inspect_asset_references(schema, extended, {extension});
        require(impact.references.empty() &&
                    std::any_of(impact.uninspected.begin(), impact.uninspected.end(),
                                [](const auto& issue) {
                                    return issue.find("editor_layout.positions") !=
                                           std::string::npos;
                                }),
                "Reference review interpreted opaque graph extensions");
        const auto before_remove = doc->source().document;
        doc->edit(doc->revision(), "Remove state with layout", [](Json& source) {
            std::erase_if(source["states"].get_ref<Json::array_t&>(),
                          [](const auto& row) { return row.at("id") == 2; });
            asset_detail::remove_state_graph_position(source, 2);
        });
        require(doc->source().document["editor_layout"]["positions"].empty(),
                "Deleted state left a dangling graph position");
        doc->undo();
        require(doc->source().document == before_remove, "Remove/layout were not one history step");
        auto opaque = fixture(AssetId::generate());
        opaque.document["editor_layout"] = {{"format", "extension.layout"}, {"ref", extension}};
        opaque.validate();
        const auto untouched = opaque.document;
        rejects([&] { asset_detail::set_state_graph_position(opaque.document, 1, {0, 0}); });
        require(opaque.document == untouched, "Graph overwrote an opaque layout extension");
        std::uint32_t cooked_version = 0;
        const auto roundtrip = AnimationStatesSource::parse_cooked(opaque.cook(), cooked_version);
        require(roundtrip.document == untouched && cooked_version == 2,
                "Cooking erased an opaque layout extension");
        auto legacy = fixture(AssetId::generate());
        legacy.document["version"] = 1;
        legacy.document["states"][1]["exit"] = {{"foreign", extension}};
        const auto legacy_cook = legacy.cook();
        asset_detail::set_state_graph_position(legacy.document, 2, {35, 75});
        legacy.validate();
        require(legacy.cook() == legacy_cook && legacy.compiled().state(2).exit_state == 0,
                "Editor layout interpreted legacy opaque exits");
        std::cout << "State graph layout bounds, stable bindings, history/reopen, stale gestures, "
                     "opaque preservation and unchanged cook passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
