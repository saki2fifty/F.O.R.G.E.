#include "asset_bytes.hpp"
#include "navigation_bake.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation_components.hpp>
#include <forge/scene.hpp>
#include <iostream>

using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void run(const std::filesystem::path& root, const std::filesystem::path& worker) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto id = authoring_command(scene, "entity.create",
                                      {{"name", "Walkable floor"}, {"recipe", "primitive.3"}})
                        .at("selected")
                        .get<std::string>();
    const auto floor = scene.entity(id);
    floor.set<LocalScale>({20, 1, 20}).set<NavigationSurface>({true});
    const auto logical = AssetId::generate();
    const NavigationSettings settings;
    const auto temporary = root / "RuntimeScratch";
    const auto prepare = [&](std::stop_token stop = {}) {
        const auto source = capture(scene.effective_document(), settings);
        check(source.meshes.empty() && source.maps.empty() && source.collisions.empty(),
              "Primitive bake fixture unexpectedly needs authored resource files");
        const auto geometry = navigation_detail::geometry(
            source, Json::array(), [](AssetId) -> const MeshResourceData& {
                throw std::runtime_error("Unexpected external Mesh in primitive bake");
            });
        auto metadata = navigation_bake_metadata(logical, source, geometry, settings, Json::array(),
                                                 Json::array(), Json::array());
        return prepare_navigation_bake(geometry, std::move(metadata), worker, temporary, stop);
    };
    auto first = prepare();
    const auto stage = first->staging.path;
    check(std::filesystem::is_regular_file(stage / "navigation.fnav") && first->staging.owner,
          "Prepared bake lost its staged bytes or producer lease");
    check(first->artifact_digest == asset_detail::content_digest(asset_detail::read_bytes(
                                        stage / "navigation.fnav", max_nav_bytes + 65556)),
          "Prepared artifact digest differs from its admitted bytes");
    const auto original = first->data.mesh;
    const auto original_metadata = first->data.metadata;
    check(original_metadata.at("asset_id") == Json(logical) &&
              original_metadata.at("source_scene") == Json(scene.asset_id()),
          "Shared bake changed logical Nav or source Scene identity");
    Query before(original);
    check(before.path({-6, 0, 0}, {6, 0, 0}).status == NavStatus::Success,
          "Prepared floor is not independently queryable");
    first.reset();
    check(!std::filesystem::exists(stage) &&
              before.path({-6, 0, 0}, {6, 0, 0}).status == NavStatus::Success,
          "Stage cleanup removed immutable query data or retained owned payload");
    floor.set<LocalTranslation>({40, 0, 0});
    auto second = prepare();
    Query after(second->data.mesh);
    check(second->data.metadata.at("asset_id") == Json(logical) &&
              second->data.metadata.at("geometry_sha256") !=
                  original_metadata.at("geometry_sha256") &&
              after.path({34, 0, 0}, {46, 0, 0}).status == NavStatus::Success &&
              before.path({-6, 0, 0}, {6, 0, 0}).status == NavStatus::Success,
          "Changed geometry did not produce an independent basis under the same logical identity");
    const auto second_stage = second->staging.path;
    Query untouched(original);
    const auto previous_generation = before.generation();
    before.replace_mesh(second->data.mesh);
    check(before.generation() > previous_generation &&
              before.path({34, 0, 0}, {46, 0, 0}).status == NavStatus::Success &&
              untouched.path({-6, 0, 0}, {6, 0, 0}).status == NavStatus::Success,
          "Query basis adoption retained cached routes or changed another query");
    const auto adopted_generation = before.generation();
    bool invalid = false;
    try {
        before.replace_mesh({});
    } catch (const std::exception&) {
        invalid = true;
    }
    check(invalid && before.generation() == adopted_generation &&
              before.path({34, 0, 0}, {46, 0, 0}).status == NavStatus::Success,
          "Rejected basis replacement disturbed the last usable query");
    second.reset();
    check(!std::filesystem::exists(second_stage), "Second staged bake was not retired");
    std::stop_source cancelled;
    cancelled.request_stop();
    bool rejected = false;
    try {
        (void)prepare(cancelled.get_token());
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && std::filesystem::is_empty(temporary / ".forge/navigation-staging"),
          "Cancelled bake launched work or retained its owned stage");
    check(!std::filesystem::exists(temporary / "forge.assets.json") &&
              !std::filesystem::exists(temporary / "forge.project.json"),
          "Shared bake required or created an authoring database in temporary storage");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            return 2;
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        run(root, std::filesystem::absolute(argv[2]));
        std::filesystem::remove_all(root);
        std::cout << "Shared navigation bake: detached storage, stable identity, changed geometry, "
                     "independent immutable queries, cancellation and lease retirement PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
