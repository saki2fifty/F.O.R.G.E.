#include "builtins.hpp"
#include "physics_overlay.hpp"
#include "tile_map_cook.hpp"
#include <chrono>
#include <forge/physics.hpp>
#include <imgui_internal.h>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void require(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
void exercise(const std::filesystem::path& project, float scale) {
    EngineContext engine{WorldRole::Authoring, false, {physics_schema_module()}};
    Scene scene(engine.world());
    const auto id = EntityId::generate().str();
    scene.reset(
        {{"version", 5},
         {"asset_id", AssetId::generate()},
         {"entities", Json::array({{{"id", id},
                                    {"name", "Planar sensor"},
                                    {"spatial", {{"mode", "world"}}},
                                    {"components",
                                     {{"forge.local_translation", {{"x", 0}, {"y", 0}, {"z", 7}}},
                                      {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 0}}},
                                      {"forge.physics_body_2d", {{"sensor", true}}},
                                      {"forge.circle_collider_2d", {{"radius", .5}}}}}}})}});
    const auto authored = scene.document();
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::project_index(project));
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.f / 60;
    io.FontGlobalScale = scale;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::interface_scale = scale;
    ui::PhysicsOverlay overlay;
    overlay.visible = true;
    ui::PublishedTileMapView map_view;
    unsigned map_reads = 0;
    const ui::PublishedTileMapReader read_map = [&](AssetRef<TileMapAsset> ref) {
        require(map_view.cells && ref.id == map_view.cells->asset(),
                "Overlay requested the wrong Map root");
        ++map_reads;
        return map_view;
    };
    EditorCamera camera;
    camera.target = {0, 0, 7};
    camera.distance = 8;
    auto draw = [&](const Json& snapshot, const std::string& selected) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({800, 600}, ImGuiCond_Always);
        ImGui::Begin("Planar Scene", nullptr, ImGuiWindowFlags_NoScrollbar);
        const auto vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        const auto commands = ImGui::GetWindowDrawList()->CmdBuffer.Size;
        const auto active = ImGui::GetCurrentContext()->ActiveId;
        overlay.draw(snapshot, selected, camera, {20, 40}, {760, 520}, project, catalog, {},
                     read_map);
        require(active == ImGui::GetCurrentContext()->ActiveId, "Planar guide claimed UI input");
        const auto added = ImGui::GetWindowDrawList()->VtxBuffer.Size - vertices;
        for (int i = commands; i < ImGui::GetWindowDrawList()->CmdBuffer.Size; ++i) {
            const auto& command = ImGui::GetWindowDrawList()->CmdBuffer[i];
            if (command.ElemCount)
                require(command.ClipRect.x >= 20 && command.ClipRect.y >= 40 &&
                            command.ClipRect.z <= 780 && command.ClipRect.w <= 560,
                        "Planar guide escaped Scene clipping");
        }
        for (const auto& vertex : ImGui::GetWindowDrawList()->VtxBuffer)
            require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y),
                    "Planar guide projected nonfinite vertices");
        ImGui::End();
        ImGui::Render();
        return added;
    };
    auto await = [&](const Json& snapshot, bool success) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            const auto added = draw(snapshot, id);
            if (success && overlay.ready())
                return added;
            if (!success && overlay.status().find("equal absolute XY scale") != std::string::npos)
                return added;
            require(std::chrono::steady_clock::now() < deadline, overlay.status().c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    const auto snapshot = scene.effective_document();
    require(await(snapshot, true) > 0 && overlay.status() == "2D sensor - authored outline",
            "Actual planar overlay/status missing");
    // Disabled 3D asset choices must not start resource IO or hide the 2D shape.
    auto mixed = snapshot;
    mixed["entities"][0]["components"]["forge.physics_body"] = {{"enabled", false}};
    mixed["entities"][0]["components"]["forge.asset_collider"] = {{"asset", nullptr}};
    require(await(mixed, true) > 0 && overlay.status() == "2D sensor - authored outline",
            "Inactive 3D asset collider blocked planar preview");
    auto invalid = snapshot;
    invalid["entities"][0]["world_affine"][5] = 2;
    invalid["entities"][0]["components"]["forge.local_scale"]["y"] = 2;
    require(await(invalid, false) > 0 && !overlay.ready(), "Invalid physical scale claimed ready");
    require(await(snapshot, true) > 0, "Corrected authored outline did not recover");
    auto disabled = snapshot;
    disabled["entities"][0]["components"]["forge.physics_body_2d"]["enabled"] = false;
    require(await(disabled, true) > 0 &&
                overlay.status() == "2D collision disabled - authored outline",
            "Disabled planar source preview lost its status");
    auto removed = snapshot;
    removed["entities"][0]["components"].erase("forge.physics_body_2d");
    draw(removed, id);
    require(!overlay.ready(), "Removed body retained ready collision preview");
    await(snapshot, true);
    draw(snapshot, "");
    require(!overlay.ready(), "Empty selection retained ready collision preview");
    require(scene.document() == authored && !engine.services().available(Capability::Physics),
            "Read-only planar preview mutated source or created a solver");
    auto map_source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    const std::array<TileMapCellEdit, 1> painted{{{1, {1, -1}, {17, 7}}}};
    map_source = map_source.paint(painted);
    map_view = {std::make_shared<const TileMapData>(
                    decode_cooked_tile_map(cook_tile_map(map_source), map_source.asset())),
                "first-published", "", true};
    auto map_scene = snapshot;
    auto& components = map_scene["entities"][0]["components"];
    components.erase("forge.circle_collider_2d");
    components["forge.physics_body_2d"]["sensor"] = false;
    components["forge.tile_map_collider_2d"] = {{"layer", 1}};
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == "forge.tile_map_renderer")
            components["forge.tile_map_renderer"] = type.defaults;
    components["forge.tile_map_renderer"]["map"] = map_source.asset();
    require(await(map_scene, true) > 0 && map_reads &&
                overlay.status() == "Tile Map collision - published layer outline",
            "Published Map did not reach the shared async Scene outline");
    map_view.current = false;
    map_view.status = "Preparing replacement published Map";
    require(draw(map_scene, id) > 0 && !overlay.ready() && overlay.status() == map_view.status,
            "Stale published Map claimed current ready geometry");
    const std::array<TileMapCellEdit, 1> erase{{{1, {1, -1}, {}}}};
    map_view.cells = std::make_shared<const TileMapData>(map_view.cells->with_erased_cells(erase));
    map_view.revision = "second-published";
    map_view.current = true;
    await(map_scene, true);
    require(overlay.ready() &&
                overlay.status().find("selected layer is empty") != std::string::npos,
            "Valid empty published layer retained old collision or failed readiness");
    require(scene.document() == authored && !engine.services().available(Capability::Physics),
            "Published Map outline changed authoring source or created a solver");
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Provide build-owned scratch root");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(root, scale);
        std::filesystem::remove_all(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout
        << "Actual ImGui planar overlay:100/150/200% contexts, async geometry, status, clipping, "
           "correction/removal and read-only source passed; no GPU/OS acceptance claimed\n";
}
