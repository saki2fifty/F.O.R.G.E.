#include "physics_overlay.hpp"
#include <forge/authoring.hpp>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
double error(Double3 a, Double3 b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
Json& row(Json& scene, const std::string& id) {
    for (auto& e : scene.at("entities"))
        if (e.at("id") == id)
            return e;
    throw std::runtime_error("Missing fixture entity");
}
void check_guides() {
    EngineContext engine;
    Scene scene{engine.world()};
    scene.reset(
        {{"version", 1},
         {"entities",
          Json::array(
              {{{"id", "p"}, {"name", "Parent"}, {"components", Json::object()}},
               {{"id", "a"}, {"name", "Anchor"}, {"parent", "p"}, {"components", Json::object()}},
               {{"id", "b"}, {"name", "Attached"}, {"components", Json::object()}},
               {{"id", "j"}, {"name", "Joint"}, {"components", Json::object()}}})}});
    const float half = std::sqrt(.5f);
    scene.entity("p").set<LocalTranslation>({10, 20, 30});
    scene.entity("p").set<LocalRotation>({0, 0, half, half});
    scene.entity("p").set<LocalScale>({-2, 3, 4});
    scene.entity("a").set<LocalTranslation>({1, 2, 3}).set<PhysicsBody>({}).set<SpatialBinding>(
        {SpatialMode::FollowStructure, {}});
    PhysicsBody moving;
    moving.motion = 2;
    scene.entity("b").set<PhysicsBody>(moving).set<LocalTranslation>({2, 3, 4});
    scene.entity("b").set<LocalRotation>({0, half, 0, half}).set<LocalScale>({-2, 3, 4});
    PhysicsJoint joint;
    joint.kind = 3;
    joint.body_a = *engine.world().reference(scene.entity("a"));
    joint.body_b = *engine.world().reference(scene.entity("b"));
    joint.anchor_a_x = .5f;
    joint.anchor_a_y = 1;
    joint.anchor_a_z = -.25f;
    joint.anchor_b_x = .5f;
    scene.entity("j").set<PhysicsJoint>(joint);
    // A joint entity's own transform is unrelated to either physical frame.
    scene.entity("j").set<LocalTranslation>({900, 800, 700});
    const auto id = scene.canonical_id("j"), a = scene.canonical_id("a"),
               b = scene.canonical_id("b");
    const auto before = scene.document();
    const auto snapshot = scene.effective_document();
    const auto guide = ui::prepare_physics_joint_guide(snapshot, id);
    require(guide && guide->valid, "Valid selected joint guide unavailable");
    // Independent parent matrix: T(10,20,30) Rz(90) S(-2,3,4).
    require(error(guide->bodies[0].anchor, {1, 17, 41}) < 1e-5,
            "Guide anchor ignored rotated parent or signed scale");
    require(error(guide->bodies[1].anchor, {2, 3, 5}) < 1e-5,
            "Guide anchor ignored reflected local body scale");
    require(error(guide->bodies[1].axes[0], {0, 0, -1}) < 1e-5,
            "Guide rigid axis incorrectly included scale reflection");
    for (const auto& frame : guide->bodies) {
        for (const auto& axis : frame.axes)
            require(std::abs(error(axis, {}) - 1) < 1e-5, "Guide axis is not a unit vector");
        const auto& x = frame.axes[0];
        const auto& y = frame.axes[1];
        require(std::abs(x[0] * y[0] + x[1] * y[1] + x[2] * y[2]) < 1e-5,
                "Guide rigid frame is not orthogonal");
    }
    require(!ui::prepare_physics_joint_guide(snapshot, a), "Nonjoint generated joint guide");
    auto altered = snapshot;
    row(altered, b)["components"]["forge.physics_body"]["enabled"] = false;
    require(!ui::prepare_physics_joint_guide(altered, id)->valid &&
                ui::prepare_physics_joint_guide(altered, id)->bodies[1].available,
            "Disabled body lost authored anchor or claims valid pairing");
    altered = snapshot;
    row(altered, b)["components"]["forge.physics_body"]["sensor"] = true;
    require(!ui::prepare_physics_joint_guide(altered, id)->valid,
            "Sensor guide claims valid joint");
    altered = snapshot;
    row(altered, id)["components"]["forge.physics_joint"]["body_b"] = nullptr;
    require(!ui::prepare_physics_joint_guide(altered, id)->bodies[1].available,
            "Empty reference produced anchor");
    altered = snapshot;
    row(altered, id)["components"]["forge.physics_joint"]["body_b"] =
        EntityRef{AssetId::generate(), joint.body_b.entity};
    require(ui::prepare_physics_joint_guide(altered, id)->bodies[1].error ==
                "Body is in another scene",
            "Other-scene reference reinterpreted as local body");
    altered = snapshot;
    row(altered, b)["id"] = EntityId::generate();
    require(!ui::prepare_physics_joint_guide(altered, id)->bodies[1].available,
            "Missing body produced anchor");
    altered = snapshot;
    const auto duplicate = row(altered, b);
    altered["entities"].push_back(duplicate);
    require(ui::prepare_physics_joint_guide(altered, id)->bodies[1].error ==
                "Body reference is ambiguous",
            "Ambiguous reference silently selected a body");
    altered = snapshot;
    row(altered, b)["spatial_resolved"] = false;
    require(!ui::prepare_physics_joint_guide(altered, id)->bodies[1].available,
            "Unresolved body produced anchor");
    altered = snapshot;
    row(altered, b)["components"]["forge.physics_body"]["motion"] = 0;
    require(!ui::prepare_physics_joint_guide(altered, id)->valid,
            "Two static bodies claim valid joint");
    altered = snapshot;
    row(altered, id)["components"]["forge.physics_joint"]["normal_a"] = joint.axis_a;
    require(!ui::prepare_physics_joint_guide(altered, id)->valid,
            "Invalid fixed frame claims valid guide");
    altered = snapshot;
    row(altered, id)["components"]["forge.physics_joint"]["body_b"] = joint.body_a;
    require(!ui::prepare_physics_joint_guide(altered, id)->valid, "Self-joint claims valid guide");
    // Actual overlay draw at compact/normal/large interface scales. No catalog,
    // native solver, asset cook, UI command or document/history write is needed.
    for (const float scale : {.65f, 1.f, 2.f}) {
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
        EditorCamera camera;
        camera.target = {1, 17, 41};
        camera.distance = 60;
        auto draw = [&](const Json& value) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({800, 600}, ImGuiCond_Always);
            ImGui::Begin("Joint Scene", nullptr, ImGuiWindowFlags_NoScrollbar);
            const auto count = ImGui::GetWindowDrawList()->VtxBuffer.Size;
            const auto command = ImGui::GetWindowDrawList()->CmdBuffer.Size;
            const auto active = ImGui::GetCurrentContext()->ActiveId;
            overlay.draw(value, id, camera, {20, 40}, {760, 520}, {}, {});
            require(active == ImGui::GetCurrentContext()->ActiveId,
                    "Guide draw claimed active UI input");
            const auto added = ImGui::GetWindowDrawList()->VtxBuffer.Size - count;
            for (int i = command; i < ImGui::GetWindowDrawList()->CmdBuffer.Size; ++i) {
                const auto& cmd = ImGui::GetWindowDrawList()->CmdBuffer[i];
                if (cmd.ElemCount)
                    require(cmd.ClipRect.x >= 20 && cmd.ClipRect.y >= 40 && cmd.ClipRect.z <= 780 &&
                                cmd.ClipRect.w <= 560,
                            "Guide draw escaped the Scene clipping rectangle");
            }
            for (const auto& vertex : ImGui::GetWindowDrawList()->VtxBuffer)
                require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y),
                        "Overlay generated nonfinite projection");
            ImGui::End();
            ImGui::Render();
            return added;
        };
        const auto visible_vertices = draw(snapshot);
        require(visible_vertices > 0 && overlay.status().find("Authored Fixed joint") == 0 &&
                    overlay.status().find("anchor gap") != std::string::npos && !overlay.ready(),
                "Actual authored overlay absent or claims collision realization");
        require(draw(altered) > 0 && overlay.status().find("distinct bodies") != std::string::npos,
                "Invalid pairing lost visible diagnostic");
        auto invalid = snapshot;
        row(invalid, id)["components"]["forge.physics_joint"]["kind"] = 5;
        require(draw(invalid) > 0 && overlay.status().starts_with("Authored joint") &&
                    overlay.status().find("disabled") == std::string::npos,
                "Invalid settings received an incorrect kind/disabled label");
        // Entire geometry behind the near plane: only the clipped status text remains.
        camera.distance = -100;
        const auto behind_vertices = draw(snapshot);
        require(behind_vertices > 0 && behind_vertices < visible_vertices,
                "Behind-camera geometry was not suppressed or lost its status");
        overlay.joint_visible = false;
        require(draw(snapshot) == 0, "Joint toggle failed to suppress overlay");
        ImGui::DestroyContext();
    }
    require(scene.document() == before && !scene.undo(),
            "Guide draw mutated authored state/history");
}
} // namespace
int main() {
    try {
        check_guides();
        std::cout << "Joint anchor/frame derivation, diagnostics and scaled Scene overlay passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
