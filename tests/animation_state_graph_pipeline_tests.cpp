#include "animation_states_authoring.hpp"
#include "animation_states_layout.hpp"
#include "animation_states_resource.hpp"
#include "runtime_package.hpp"
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct World {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<AnimationRuntime> animation;
    World(const std::filesystem::path& root, const Json& source)
        : engine(WorldRole::Runtime, false, {animation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene), animation(animation_runtime(engine.world())) {
        scene.restore_snapshot(source);
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (!animation->prepare_initial_pose()) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Graph runtime admission timed out");
            std::this_thread::sleep_for(1ms);
        }
        simulation.reset_presentation();
    }
};
Json poses(World& world, const Json& source) {
    // Each positive tick already admits and writes the complete fixed pose.
    // Read the animation owner's pose directly; render snapshots have a separate gate.
    auto result = Json::object();
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.animator")) {
            const auto id = row.at("id").get<std::string>();
            result[id] = world.animation->presentation(world.scene.entity(id).id(), 1);
            require(!result[id].is_null(), "A graph character has no complete pose");
        }
    require(result.size() == 3, "Need three timed Gallery characters");
    return result;
}
Json events(World& world) {
    auto result = Json::array();
    for (const auto& e : world.animation->events())
        result.push_back({{"scene", e.entity.scene},
                          {"entity", e.entity.entity},
                          {"states", e.states},
                          {"clip", e.clip},
                          {"state", e.state},
                          {"marker", e.marker},
                          {"tick", e.tick},
                          {"offset", e.offset},
                          {"phase", e.phase},
                          {"value", e.value},
                          {"name", e.name}});
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and graph pipeline scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json scene;
        std::ifstream(root / "Scenes/30-animation-exits.scene.json") >> scene;
        auto lease = std::make_shared<ProjectLease>(root);
        AnimationStatesDocument document(lease, "Assets/TimedLocomotion.states.json");
        const auto asset = document.source().asset();
        const auto original = document.source().document;
        const auto cooked = document.source().cook();
        const auto before = AssetCatalog::open_project(root).records().at(asset);
        std::vector<Json> expected_poses, expected_events;
        // Cover 12 seconds of all three cycles. Fine fixed-step boundary behavior
        // has its own state-runtime gate; this gate proves editor layout isolation.
        constexpr float dt = .2f;
        constexpr unsigned ticks = 60;
        std::cerr << "Checking graph baseline animation cycle\n";
        {
            World baseline(root, scene);
            for (unsigned tick = 0; tick < ticks; ++tick) {
                baseline.simulation.tick(dt);
                expected_poses.push_back(poses(baseline, scene));
                expected_events.push_back(events(baseline));
            }
        }
        std::cerr << "Checking graph layout publication and typed admission\n";
        document.edit(document.revision(), "Arrange graph", [](Json& source) {
            for (const auto id : {1u, 2u, 3u})
                asset_detail::set_state_graph_position(source, id, {double(id) * 150, 75});
        });
        require(document.source().cook() == cooked, "Layout changed compiled state semantics");
        document.save();
        AssetImportService imports(lease, animation_states_import_registry(),
                                   {"windows", "d3d12", "desktop"}, 1);
        const auto draft = imports.prepare(document.locator(), {}, asset);
        const auto plan = draft.importer->discover(draft.request, {});
        require(plan.input.importer_revision == "forge.animation.states.v5",
                "Layout cook change did not advance its immutable recipe revision");
        // Simulate an older importer that preserved this then-opaque root. The
        // same source bytes must not reuse its different compiled artifact.
        auto legacy_input = plan.input;
        legacy_input.importer_revision = "forge.animation.states.v4";
        const auto legacy_text = Json{{"format", "forge.animation-states"},
                                      {"version", 2},
                                      {"document", document.source().document}}
                                     .dump();
        const auto legacy_span = std::as_bytes(std::span(legacy_text));
        const std::vector<std::byte> legacy_bytes(legacy_span.begin(), legacy_span.end());
        DerivedDataCache legacy_cache(root);
        legacy_cache.publish(legacy_input, {{"states.json", legacy_bytes}},
                             [&](const auto& artifact) { draft.importer->validate(artifact); });
        require(legacy_input.key() != plan.input.key(), "New cook reused the old cache identity");
        imports.submit(
            draft,
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_animation_states_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(15s), "Graph publication timeout");
        const auto receipts = imports.poll();
        require(receipts.size() == 1 && receipts[0].published,
                receipts.empty() ? "Missing graph receipt" : receipts[0].diagnostic);
        const auto catalog = AssetCatalog::open_project(root);
        const auto& record = catalog.records().at(asset);
        require(legacy_cache.load_selected(legacy_input.key(), [](const auto&) {}).files[0].bytes ==
                    legacy_bytes,
                "Publication rewrote an older immutable cache entry");
        require(before.metadata.at("forge.import").at("key") !=
                        record.metadata.at("forge.import").at("key") &&
                    before.metadata.at("forge.animation_states") ==
                        record.metadata.at("forge.animation_states"),
                "Layout publication lost source provenance or changed cooked bytes");
        ResourcePool<AnimationStatesAsset> pool;
        const auto ticket = request_animation_states(pool, root, record);
        require(pool.wait(ticket, 10s) && bool(pool.acquire(ticket)),
                "Published graph failed typed resource admission");
        std::cerr << "Checking graph published cycle and cold fade recovery\n";
        World revised(root, scene);
        std::set<std::uint32_t> observed;
        bool recovered_fade = false;
        for (unsigned tick = 0; tick < ticks; ++tick) {
            revised.simulation.tick(dt);
            const auto actual = poses(revised, scene);
            require(actual == expected_poses[tick] && events(revised) == expected_events[tick],
                    "Published layout changed complete poses, clocks, fades or markers");
            for (const auto& e : revised.animation->events())
                observed.insert(e.state);
            if (!recovered_fade)
                for (const auto& row : scene.at("entities")) {
                    if (!row.at("components").contains("forge.animator"))
                        continue;
                    const auto entity = revised.scene.entity(row.at("id"));
                    const auto ref = revised.engine.world().reference(entity.id());
                    const auto playback = ref ? revised.animation->playback(*ref) : std::nullopt;
                    if (!playback || !playback->transitioning)
                        continue;
                    require(revised.animation->checkpoint_ready(),
                            "Graph fade checkpoint unavailable");
                    World recovered(root, revised.scene.snapshot());
                    recovered.animation->restore(revised.animation->checkpoint());
                    require(poses(recovered, scene) == actual,
                            "Cold graph fade recovery changed poses");
                    recovered_fade = true;
                    break;
                }
        }
        require(observed == std::set<std::uint32_t>{1, 2, 3} && recovered_fade,
                "Graph pipeline did not cover the full cycle and an active fade");
        std::cerr << "Checking graph source-free relocated export\n";
        const std::array roots{scene.at("asset_id").get<AssetId>()};
        prepare_runtime_content_catalog(*lease, roots, {}, revised.scene.schema());
        const auto output = root.parent_path() / ("graph-export-" + AssetId::generate().str());
        package_runtime_content(root, output, roots, {"windows", "d3d12"}, {}, {},
                                revised.scene.schema());
        const auto relocated =
            root.parent_path() / ("graph-relocated-" + AssetId::generate().str());
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(root / "Assets", root / "Assets-offline");
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        require(shipped.records().contains(asset) &&
                    !std::filesystem::exists(relocated / document.locator()),
                "Export omitted cooked states or shipped editor source");
        DerivedDataCache cache(relocated);
        const auto artifact = cache.load_selected(
            record.metadata.at("forge.import").at("key").get<std::string>(), [](const auto&) {});
        std::uint32_t version;
        const auto runtime_source =
            AnimationStatesSource::parse_cooked(artifact.files[0].bytes, version);
        require(version == 2 && runtime_source.document == original &&
                    !runtime_source.document.contains("editor_layout"),
                "Export shipped layout metadata or changed timed animation semantics");
        World exported(relocated, scene);
        for (unsigned tick = 0; tick < ticks; ++tick) {
            exported.simulation.tick(dt);
            require(poses(exported, scene) == expected_poses[tick] &&
                        events(exported) == expected_events[tick],
                    "Source-free relocated export changed graph animation behavior");
        }
        std::cout
            << "Graph publication preserves complete poses, clocks, fades, all entry markers, "
               "cold recovery and source-free relocated export; layout remains editor-only\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
