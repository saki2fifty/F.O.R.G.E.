#pragma once
#include "authoring.hpp"
#include "blockout.hpp"
#include "camera_controls.hpp"
#include "scene_cache.hpp"
#include "scene_tools.hpp"
#include "view_state.hpp"
#include <limits>

void require(bool, const char*);
inline void test_orthographic_camera() {
    using namespace forge;
    const auto close = [](double a, double b) { return std::abs(a - b) < .001; };
    EditorCamera camera;
    const auto old_eye = camera.eye();
    require(camera.set_projection(CameraProjection::Orthographic) &&
                close(camera.orthographic_height, 12 / std::sqrt(3.)),
            "Projection switch did not preserve focus-plane size");
    require(camera.eye() == old_eye, "Entering orthographic moved the eye");
    require(camera.set_projection(CameraProjection::Perspective) && close(camera.distance, 6),
            "Returning to perspective changed the focus-plane size");
    camera.set_projection(CameraProjection::Orthographic);
    camera.target = {2, 3, 4};
    camera.orthographic_height = 8;
    for (const auto size : {std::array<float, 2>{800, 600}, {600, 800}, {1024, 256}}) {
        const auto w = size[0], h = size[1];
        const auto center = camera.ray(w / 2, h / 2, w, h);
        const auto corner = camera.ray(w * .75f, h * .25f, w, h);
        require(center && corner && center->direction == EditorCamera::Vec{0, 0, 1} &&
                    corner->direction == center->direction,
                "Orthographic rays do not share the expected forward direction");
        require(close(center->origin[0], 2) && close(center->origin[1], 3) &&
                    close(center->origin[2], -2) && close(corner->origin[0], 2 + 2 * w / h) &&
                    close(corner->origin[1], 5) && close(corner->origin[2], -2),
                "Orthographic screen rays did not shift their origins by the visible extent");
        for (float z : {4.f, 14.f, 104.f}) {
            const auto pixel = project_point(camera, {2 + 2 * w / h, 5, z}, w, h);
            require(pixel && close((*pixel)[0], .75 * w) && close((*pixel)[1], .25 * h),
                    "Orthographic projection changes size with depth or aspect");
        }
        require(camera.ray(-50, -30, w, h).has_value(),
                "A continuing drag outside the image lost its ray");
    }
    const auto origin = camera.target;
    camera.pan(100, 50, 600);
    require(close(camera.target[0], origin[0] - 8. / 6) &&
                close(camera.target[1], origin[1] + 8. / 12) && camera.target[2] == origin[2],
            "Orthographic panning did not follow visible world units");
    const auto before_zoom = camera.distance;
    camera.zoom(1);
    require(camera.distance == before_zoom && close(camera.orthographic_height, 8 * std::exp(-.15)),
            "Orthographic zoom moved the eye instead of changing visible extent");
    for (int i = 0; i < 20; ++i)
        camera.zoom(1000);
    require(camera.orthographic_height == EditorCamera::minimum_height,
            "Orthographic minimum zoom was not bounded");
    for (int i = 0; i < 20; ++i)
        camera.zoom(-1000);
    require(camera.orthographic_height == EditorCamera::maximum_height,
            "Orthographic maximum zoom was not bounded");
    camera.set_projection(CameraProjection::Perspective);
    require(camera.distance == 100000, "Perspective switch did not clamp its zoom range");
    camera.set_projection(CameraProjection::Orthographic);
    for (const auto aspect : {.5f, 1.f, 2.f}) {
        require(camera.frame_sphere({3, 4, 5}, 2, aspect),
                "Orthographic framing rejected a sphere");
        require(close(camera.orthographic_height, 4.4 / std::min(1.f, aspect)) &&
                    camera.distance >= 2 + EditorCamera::near_plane,
                "Orthographic framing cropped the sphere or near plane");
        for (const auto point : {EditorCamera::Vec{1, 4, 5}, {5, 4, 5}, {3, 2, 5}, {3, 6, 5}}) {
            const auto pixel = project_point(camera, point, 600 * aspect, 600);
            require(pixel && (*pixel)[0] > 0 && (*pixel)[0] < 600 * aspect && (*pixel)[1] > 0 &&
                        (*pixel)[1] < 600,
                    "Framed sphere exceeds an orthographic viewport");
        }
    }
    const auto key = viewport_frame_key(1, 800, 600, camera);
    auto other = camera;
    other.projection = CameraProjection::Perspective;
    require(key != viewport_frame_key(1, 800, 600, other), "Projection kept a retained image");
    other = camera;
    other.orthographic_height *= 2;
    require(key != viewport_frame_key(1, 800, 600, other), "Extent kept a retained image");
    require(key != viewport_frame_key(1, 800, 600, camera, {true, 1, GridPlane::XY}),
            "Grid plane kept a retained image");
    for (unsigned axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1}) {
            camera.align(axis, sign);
            const auto ray = camera.ray(400, 300, 800, 600);
            require(ray && close(ray->direction[axis], -sign), "Axis view has the wrong direction");
            const auto pixel = project_point(camera, camera.target, 800, 600);
            require(pixel && close((*pixel)[0], 400) && close((*pixel)[1], 300),
                    "Axis view lost its target center");
        }
    const auto valid = camera;
    for (float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        require(!camera.ray(bad, 2, 800, 600) && !camera.ray(2, 2, bad, 600) &&
                    !project_point(camera, camera.target, 800, bad),
                "Nonfinite viewport inputs were admitted");
        camera.zoom(bad);
        require(camera.distance == valid.distance &&
                    camera.orthographic_height == valid.orthographic_height,
                "Nonfinite zoom changed the camera");
    }
    require(!camera.ray(0, 0, 0, 600) && !camera.ray(0, 0, 800, 0) &&
                !camera.frame_sphere({0, 0, 0}, 100001, 1) && camera.target == valid.target &&
                camera.distance == valid.distance &&
                camera.orthographic_height == valid.orthographic_height &&
                !camera.set_projection(CameraProjection(99)),
            "Rejected view update partially changed camera state");
}

inline void test_orthographic_bookmarks(const std::filesystem::path& scratch) {
    using namespace forge;
    const auto root = scratch / ("orthographic-" + AssetId::generate().str());
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    } cleanup{root};
    SceneDocument::create_project(root, "View tests");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto authored = scene.document();
    const auto revision = scene.revision();
    EditorCamera camera;
    camera.set_projection(CameraProjection::Orthographic);
    camera.orthographic_height = 12;
    camera.target = {3, 4, 5};
    auto plane = GridPlane::XY;
    save_view(document, camera, plane);
    const auto path = project_control_file(root, "editor-views.json");
    auto data = read_json(path);
    data["extra"] = "root metadata";
    data["views"]["other.scene.json"] = {{"future", 42}};
    data["views"][view_key(document)]["future"] = "keep";
    atomic_write(path, data.dump());
    camera.orthographic_height = 7;
    save_view(document, camera, plane);
    data = read_json(path);
    require(data["extra"] == "root metadata" && data["views"]["other.scene.json"]["future"] == 42 &&
                data["views"][view_key(document)]["future"] == "keep",
            "Saving a view destroyed unrelated or unknown metadata");
    EditorCamera restored;
    auto restored_plane = GridPlane::YZ;
    require(restore_view(document, restored, &restored_plane) && restored.orthographic() &&
                restored.orthographic_height == 7 && restored.target == camera.target &&
                restored_plane == GridPlane::XY,
            "Orthographic camera and plane did not round-trip");
    auto legacy = data;
    for (auto name : {"projection", "orthographic_height", "grid_plane"})
        legacy["views"][view_key(document)].erase(name);
    atomic_write(path, legacy.dump());
    require(restore_view(document, restored, &restored_plane) && !restored.orthographic() &&
                restored.orthographic_height == 10 && restored_plane == GridPlane::XZ,
            "Legacy bookmark inherited the previous orthographic view");
    const auto before = viewport_frame_key(1, 800, 600, restored, {true, 1, restored_plane});
    for (const auto field : {"projection", "orthographic_height", "grid_plane", "distance"}) {
        auto bad = data;
        bad["views"][view_key(document)][field] =
            field == std::string("orthographic_height") || field == std::string("distance")
                ? Json(-1)
                : Json("invalid");
        atomic_write(path, bad.dump());
        bool rejected = false;
        try {
            restore_view(document, restored, &restored_plane);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected &&
                    before == viewport_frame_key(1, 800, 600, restored, {true, 1, restored_plane}),
                "Invalid bookmark partially changed camera or plane");
    }
    atomic_write(path, data.dump());
    const auto saved = read_json(path);
    auto invalid = camera;
    invalid.orthographic_height = -1;
    bool rejected = false;
    try {
        save_view(document, invalid, plane);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && read_json(path) == saved, "Rejected save replaced a usable bookmark");
    require(scene.document() == authored && scene.revision() == revision && !document.dirty(),
            "Editor views changed authored Scene data or history");
}

inline void test_orthographic_authoring() {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    const auto id = create_primitive(scene, 0, {2, 3, 4});
    const auto original = scene.document();
    EditorCamera camera;
    camera.target = {2, 3, 4};
    camera.set_projection(CameraProjection::Orthographic);
    camera.orthographic_height = 8;
    const auto snapshot = scene.effective_document();
    require(pick_block(snapshot, camera, 400, 300, 800, 600) == id &&
                pick_block(snapshot, camera, 50, 300, 800, 600).empty(),
            "Orthographic block selection reused the center ray origin");
    const auto x = axis_drag(camera, camera.target, 0, {400, 300}, {500, 300}, 800, 600);
    const auto y = axis_drag(camera, camera.target, 1, {400, 300}, {400, 400}, 800, 600);
    require(x && y && std::abs(*x - 8.f / 6) < .001f && std::abs(*y + 8.f / 6) < .001f &&
                !axis_drag(camera, camera.target, 3, {400, 300}, {500, 300}, 800, 600),
            "Orthographic transform drag lost its pixel-to-world mapping");
    const auto rig = rig_axis_drag(camera, {2, 3, 4}, {-2, 0, 0}, {400, 300}, {500, 300}, 800, 600);
    require(rig && std::abs(*rig + 2. / 3) < .001,
            "Orthographic rig drag lost shifted ray origins or mirrored basis units");
    ui::SceneTools tools;
    const auto front = tools.handles(camera, {2, 3, 4}, {800, 600});
    const auto back = tools.handles(camera, {2, 3, 104}, {800, 600});
    require(front[0] && front[3] && back[0] && back[3] &&
                std::abs((front[0]->x - front[3]->x) - (back[0]->x - back[3]->x)) < .001f,
            "Orthographic transform handle size changed with depth");
    require(scene.document() == original, "Camera/picking helpers changed authored data");
}

inline void test_orthographic_gallery_bookmarks(const std::filesystem::path& scratch) {
    using namespace forge;
    const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() /
                        "samples/projects/FeatureGallery";
    const auto root = scratch / ("gallery-views-" + AssetId::generate().str());
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    } cleanup{root};
    SceneDocument::create_project(root, "Gallery bookmark acceptance");
    std::filesystem::create_directories(root / "Scenes");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    std::filesystem::copy_file(source / ".forge/editor-views.json",
                               root / ".forge/editor-views.json");
    for (const auto* name :
         {"71-world-sprites.scene.json", "72-sprite-frame-animation.scene.json"}) {
        const auto path = std::filesystem::path("Scenes") / name;
        std::filesystem::copy_file(source / path, root / path);
        document.open_scene(root / path);
        const auto baseline = scene.document();
        const auto revision = scene.revision();
        EditorCamera camera;
        camera.target = {50, 50, 50};
        auto plane = GridPlane::YZ;
        require(restore_view(document, camera, &plane) && camera.orthographic() &&
                    camera.target == EditorCamera::Vec{0, 0, 0} && camera.yaw == 0 &&
                    camera.pitch == 0 && camera.distance == 8 && camera.orthographic_height == 8 &&
                    plane == GridPlane::XY,
                "Shipped Gallery Sprite bookmark did not restore its intended front view");
        // The saved editor view and authored game camera have separate owners.
        require(scene.document() == baseline && scene.revision() == revision && !scene.can_undo() &&
                    !document.dirty() && read_json(root / path) == read_json(source / path),
                "Gallery bookmark restoration changed authored Sprite/Game-camera content");
    }
    document.open_scene(root / "Scenes/main.scene.json");
    EditorCamera camera;
    auto plane = GridPlane::XZ;
    require(!restore_view(document, camera, &plane) && !camera.orthographic() &&
                plane == GridPlane::XZ,
            "A Scene without a bookmark acquired a Gallery projection");
}

inline void test_orthographic_view_controls(float scale) {
    using namespace forge;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1920, 1080};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/ui/LatoLatin-Regular.ttf";
    require(std::filesystem::is_regular_file(font), "Missing actual editor font");
    io.FontDefault = io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    EditorCamera camera;
    auto plane = GridPlane::XZ;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1400, 900}, ImGuiCond_Always);
        ImGui::Begin("Scene view controls");
        ui::camera_view_controls(camera, plane);
        ImGui::End();
        ImGui::Render();
    };
    for (unsigned i = 0; i < 4; ++i)
        frame();
    const auto click = [&](const std::string& id) {
        frame();
        const auto found = test::ui_targets.find(id);
        require(found != test::ui_targets.end(), "Missing Scene view control");
        const auto box = found->second;
        const ImVec2 point{(box.minimum.x + box.maximum.x) / 2,
                           (box.minimum.y + box.maximum.y) / 2};
        require(box.enabled && point.x >= box.clip_minimum.x && point.x < box.clip_maximum.x &&
                    point.y >= box.clip_minimum.y && point.y < box.clip_maximum.y,
                "Scene view control was not visible and enabled");
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    const auto key = [&](ImGuiKey value) {
        io.AddKeyEvent(value, true);
        frame();
        io.AddKeyEvent(value, false);
        frame();
    };
    click("scene:projection");
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    require(camera.orthographic(), "Projection combo did not select orthographic");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    click("scene:orthographic-height");
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    io.AddInputCharactersUTF8("12.5");
    frame();
    key(ImGuiKey_Enter);
    require(camera.orthographic_height == 12.5f,
            "Orthographic height control did not accept a typed value");
    click("scene:grid-plane");
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_Enter);
    require(plane == GridPlane::XY, "Grid-plane combo did not select XY");
    constexpr unsigned axes[] = {2, 2, 1, 1, 0, 0};
    constexpr int signs[] = {-1, 1, 1, -1, 1, -1};
    constexpr GridPlane planes[] = {GridPlane::XY, GridPlane::XY, GridPlane::XZ,
                                    GridPlane::XZ, GridPlane::YZ, GridPlane::YZ};
    for (unsigned i = 0; i < 6; ++i) {
        click("scene:axis-views");
        click("scene:axis-view:" + std::to_string(i));
        require(camera.orthographic() && plane == planes[i] &&
                    std::abs(camera.forward()[axes[i]] + signs[i]) < .00001f &&
                    camera.orthographic_height == 12.5f,
                "Axis preset did not preserve scale and select its direction/plane");
    }
    click("scene:projection");
    key(ImGuiKey_UpArrow);
    key(ImGuiKey_Enter);
    require(!camera.orthographic() &&
                std::abs(camera.distance - 12.5f * std::sqrt(3.f) / 2) < .001f,
            "Perspective control did not match the orthographic focus size");
    test::observe_ui = false;
    test::ui_targets.clear();
    ImGui::DestroyContext();
}
