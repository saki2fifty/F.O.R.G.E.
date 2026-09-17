// Exercise the production Diligent renderer using Windows' D3D12 software device.
#include <windows.h>

// Native declarations must precede Diligent's native command queue interface.
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

// Renderer interfaces.
#include "Graphics/GraphicsEngineD3D12/interface/CommandQueueD3D12.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "ImGuiImplDiligent.hpp"
#include "authoring.hpp"
#include "viewport.hpp"
#include "widgets.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
using namespace Diligent;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
void check(HRESULT result, const char* text) { require(SUCCEEDED(result), text); }
using Pixels = std::vector<std::array<unsigned char, 4>>;
Pixels readback(IRenderDevice* device, IDeviceContext* context, ITextureView* view) {
    auto* texture = view->GetTexture();
    auto desc = texture->GetDesc();
    desc.Name = "FORGE test readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    require(bool(staging), "Readback texture allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy;
    copy.pSrcTexture = texture;
    copy.pDstTexture = staging;
    copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->WaitForIdle();
    MappedTextureSubresource data;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, data);
    require(data.pData != nullptr, "Readback map failed");
    Pixels result(desc.Width * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(result.data() + y * desc.Width,
                    static_cast<const unsigned char*>(data.pData) + y * data.Stride,
                    desc.Width * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    context->FinishFrame();
    return result;
}
void save(const Pixels& pixels, unsigned width, unsigned height,
          const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (auto p : pixels)
        output.write(reinterpret_cast<const char*>(p.data()), 3);
}
void check_imgui(IRenderDevice* device, IDeviceContext* context,
                 const std::filesystem::path& images) {
    TextureDesc desc;
    desc.Name = "ImGui regression target";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 320;
    desc.Height = 120;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> target;
    device->CreateTexture(desc, nullptr, &target);
    require(bool(target), "ImGui target allocation failed");
    auto* rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    ImGuiImplDiligent gui({device, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_UNKNOWN});
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {320, 120};
    io.DeltaTime = 1.0f / 60;
    io.Fonts->AddFontDefaultBitmap();
    desc.Name = "ImGui external texture";
    desc.Width = desc.Height = 2;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_IMMUTABLE;
    const std::uint32_t green[] = {0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00};
    TextureSubResData subresource{green, 8};
    TextureData initial{&subresource, 1};
    RefCntAutoPtr<ITexture> external;
    device->CreateTexture(desc, &initial, &external);
    require(bool(external), "ImGui external texture allocation failed");
    for (float scale : {0.65f, 1.0f, 2.0f, 1.0f}) {
        forge::ui::style(scale);
        gui.NewFrame(320, 120, SURFACE_TRANSFORM_IDENTITY);
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->AddText({10, 10}, IM_COL32_WHITE, "FORGE 123");
        draw->AddImage(ImTextureRef{reinterpret_cast<ImTextureID>(
                           external->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))},
                       {240, 10}, {280, 50});
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[] = {0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        gui.Render(context);
        const auto pixels = readback(device, context, rtv);
        unsigned text_pixels = 0;
        for (unsigned y = 5; y < 80; ++y)
            for (unsigned x = 5; x < 220; ++x)
                text_pixels += pixels[y * 320 + x][0] > 100;
        require(text_pixels > 30, "ImGui font texture failed to render after scaling");
        const auto sample = pixels[25 * 320 + 250];
        require(sample[1] > 240 && sample[0] < 10 && sample[2] < 10,
                "ImGui external texture ID failed to render");
        save(pixels, 320, 120, images / ("imgui-" + std::to_string(scale) + ".ppm"));
    }
    context->WaitForIdle();
}
void check_axes(const Pixels& pixels, unsigned width, unsigned height,
                const forge::EditorCamera& camera) {
    unsigned checked = 0;
    for (unsigned axis : {0u, 2u}) {
        for (float offset : {-3.0f, 3.0f}) {
            forge::Vec3 point{};
            point[axis] = offset;
            const auto projected = forge::project_point(camera, point, float(width), float(height));
            if (!projected || (*projected)[0] < 4 || (*projected)[1] < 4 ||
                (*projected)[0] >= width - 4 || (*projected)[1] >= height - 4)
                continue;
            bool found = false;
            const int x = int((*projected)[0]), y = int((*projected)[1]);
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx) {
                    const auto p = pixels[(y + dy) * width + x + dx];
                    found |= axis == 0 ? p[0] > p[2] + 50 : p[2] > p[0] + 50;
                }
            require(found, "Rendered world axis drifted away from projected world coordinates");
            ++checked;
        }
    }
    require(checked >= 2, "Axis fixture does not cover visible world coordinates");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected image output directory");
        const std::filesystem::path images(argv[1]);
        std::filesystem::create_directories(images);
        ComPtr<IDXGIFactory4> dxgi;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)), "DXGI factory failed");
        ComPtr<IDXGIAdapter> warp;
        check(dxgi->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter unavailable");
        ComPtr<ID3D12Device> native;
        check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&native)),
              "WARP D3D12 device failed");
        ComPtr<ID3D12CommandQueue> native_queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        check(native->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&native_queue)),
              "WARP queue failed");
        auto* factory = LoadAndGetEngineFactoryD3D12();
        require(factory != nullptr, "Diligent backend unavailable");
        RefCntAutoPtr<ICommandQueueD3D12> queue;
        factory->CreateCommandQueueD3D12(native.Get(), native_queue.Get(), nullptr, &queue);
        require(bool(queue), "Diligent queue attachment failed");
        ICommandQueueD3D12* queues[] = {queue};
        EngineD3D12CreateInfo engine;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        factory->AttachToD3D12Device(native.Get(), 1, queues, engine, &device, &context);
        require(device && context, "Diligent device attachment failed");
        check_imgui(device, context, images);
        forge::Viewport viewport(device);
        forge::EngineContext scene_engine;
        forge::Scene live_scene(scene_engine.world());
        forge::Json scene{{"version", 1}, {"entities", forge::Json::array()}};
        forge::EditorCamera camera;
        camera.target = {0, 0, 0};
        camera.pitch = -.55f;
        camera.distance = 12;
        constexpr unsigned width = 640, height = 400;
        std::uint64_t generation = 1;
        auto render = [&](const char* name, forge::GridSettings grid = {}, unsigned w = 640,
                          unsigned h = 400) {
            live_scene.replace(scene);
            const auto effective = live_scene.effective_document();
            auto pixels = readback(
                device, context,
                viewport.render(context, effective, w, h, camera, generation, false, grid));
            save(pixels, w, h, images / (std::string(name) + ".ppm"));
            return pixels;
        };
        auto original = render("grid");
        check_axes(original, width, height, camera);
        auto unchanged = render("retained");
        require(unchanged == original && viewport.retained == 1, "Retained grid frame changed");
        camera.look(45, 12);
        check_axes(render("look"), width, height, camera);
        camera.pan(75, -40, height);
        check_axes(render("pan"), width, height, camera);
        camera.orbit(-50, -35);
        check_axes(render("orbit"), width, height, camera);
        camera.fly(.3f, .5f, .1f, .2f);
        check_axes(render("fly"), width, height, camera);
        camera.zoom(1);
        check_axes(render("zoom"), width, height, camera);
        camera.target = {0, 0, 0};
        camera.distance = 12;
        camera.align(1, 1);
        auto top = render("top");
        check_axes(top, width, height, camera);
        check_axes(render("resized", {}, 480, 320), 480, 320, camera);
        auto no_grid = render("hidden", {false, 1});
        require(
            std::all_of(no_grid.begin(), no_grid.end(), [&](auto p) { return p == no_grid[0]; }),
            "Hidden grid still drew geometry");
        auto spacing = render("spacing", {true, 2});
        require(spacing != top, "Spacing change retained stale grid");
        // Measure a single grid line away from crossings/axes. Thin lines may
        // straddle two pixels; their shoulders must not become a wide soft band.
        auto check_line_width = [&](const Pixels& image, unsigned w, unsigned h) {
            const auto at = forge::project_point(camera, {1, 0, .5f}, float(w), float(h));
            require(bool(at), "Line-width fixture outside view");
            const int cx = int((*at)[0]), cy = int((*at)[1]);
            int peak = 0, total = 0, visible = 0;
            for (int dx = -4; dx <= 4; ++dx) {
                const auto p = image[cy * w + cx + dx];
                const int contrast = std::max(0, int(p[0]) - int(no_grid[0][0]));
                peak = std::max(peak, contrast);
                total += contrast;
                visible += contrast > 3;
            }
            require(peak > 8, "Thin grid line is too faint to read");
            require(visible <= 2 && total <= peak * 2 + 3, "Grid line widened into a soft band");
        };
        camera.yaw = 0;
        check_line_width(render("thin-top"), width, height);
        check_line_width(render("thin-top-large", {}, 1280, 800), 1280, 800);
        camera.distance = 8;
        check_line_width(render("thin-top-close"), width, height);
        // Existing world lines retain brightness when fine/major classifications swap.
        const float transition = forge::EditorCamera::focal * height / 8;
        auto line_brightness = [&](const Pixels& image) {
            const auto at = forge::project_point(camera, {30, 0, 25}, width, height);
            require(bool(at), "LOD fixture outside view");
            double sum = 0;
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    sum += image[(int((*at)[1]) + dy) * width + int((*at)[0]) + dx][0];
            return sum / 49;
        };
        for (float level : {1.0f, 10.0f}) {
            camera.distance = transition * level * .9999f;
            const auto before_lod =
                line_brightness(render(level == 1 ? "lod-before" : "lod2-before"));
            camera.distance = transition * level * 1.0001f;
            const auto after_lod = line_brightness(render(level == 1 ? "lod-after" : "lod2-after"));
            require(std::abs(after_lod - before_lod) < 2,
                    "Grid division transition pops in brightness");
        }
        // A wide top view must show gray grid lines beyond the old +/-20-unit patch.
        camera.distance = 100;
        auto wide = render("wide");
        auto far_line = forge::project_point(camera, {30, 0, 25}, width, height);
        require(bool(far_line), "Wide grid fixture invalid");
        bool distant_grid = false;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                auto p = wide[(int((*far_line)[1]) + dy) * width + int((*far_line)[0]) + dx];
                distant_grid |=
                    p[0] > 20 && p[1] > 20 && p[2] > 20 && std::abs(int(p[0]) - int(p[2])) < 35;
            }
        require(distant_grid, "Grid still has a finite patch edge");
        // The same world Z axis must lose contrast gradually toward the horizon.
        // This catches the old height-scaled distant cutoff with no angular fade.
        camera.pitch = 0;
        camera.target = {0, 6, 12};
        camera.distance = 12;
        auto horizon = render("horizon");
        auto axis_contrast = [&](float z) {
            const auto at = forge::project_point(camera, {0, 0, z}, width, height);
            require(bool(at), "Fade fixture outside view");
            int peak = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto p = horizon[(int((*at)[1]) + dy) * width + int((*at)[0]) + dx];
                    peak = std::max(peak, int(p[2]) - int(p[0]));
                }
            return peak;
        };
        const int near_contrast = axis_contrast(20), middle_contrast = axis_contrast(60),
                  far_contrast = axis_contrast(180);
        require(near_contrast > middle_contrast + 15 && middle_contrast > far_contrast + 8,
                "Grid axes do not fade progressively toward the horizon");
        std::cout << "Horizon contrast near/middle/far: " << near_contrast << '/' << middle_contrast
                  << '/' << far_contrast << '\n';
        camera.target = {0, 0, 0};
        camera.align(1, 1);
        camera.distance = 12;
        scene["entities"].push_back(
            {{"id", "cube"},
             {"name", "Cube"},
             {"components", {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 0}}}}}});
        ++generation;
        auto cube_off = render("cube-no-grid", {false, 1});
        auto cube_on = render("cube-grid");
        require(cube_on[(height / 2) * width + width / 2] ==
                    cube_off[(height / 2) * width + width / 2],
                "Grid drew through cube at origin");
        camera.target = {0, 0, 0};
        camera.pitch = 0;
        camera.distance = 12;
        auto edge = render("edge-on");
        require(std::all_of(edge.begin(), edge.end(), [&](auto p) { return p == edge[0]; }) ==
                    false,
                "Edge-on cube disappeared");
        scene["entities"].clear();
        ++generation;
        camera.target = {0, 10, 0};
        camera.pitch = forge::EditorCamera::pole;
        camera.distance = 6;
        auto sky = render("looking-away");
        require(std::all_of(sky.begin(), sky.end(), [&](auto p) { return p == sky[0]; }),
                "Grid drew behind an upward-looking camera");
        // Hierarchical nonuniform scale + child rotation must retain shear in the
        // production renderer, picker and inverse-transpose lighting.
        live_scene.reset(forge::empty_scene());
        auto parent = forge::authoring_command(live_scene, "entity.create", forge::Json::object())
                          .at("selected")
                          .get<std::string>();
        auto child = forge::authoring_command(live_scene, "entity.create", forge::Json::object())
                         .at("selected")
                         .get<std::string>();
        live_scene.entity(parent).set<forge::LocalTranslation>({20, 0, 0});
        live_scene.entity(parent).set<forge::LocalRotation>(
            forge::rotation_from_euler({20, 25, 0}));
        live_scene.entity(parent).set<forge::LocalScale>({2, 1.5f, 1});
        live_scene.entity(child).set<forge::LocalTranslation>({-10, 0, 0});
        live_scene.entity(child).set<forge::LocalRotation>(forge::rotation_from_euler({0, 0, 35}));
        live_scene.entity(child).set<forge::Tint>({1, 1, 1});
        live_scene.reparent_entity(child, parent, forge::ReparentMode::KeepLocal);
        const auto effective = live_scene.effective_document();
        forge::Json child_view;
        for (const auto& e : effective.at("entities"))
            if (e.at("id") == child)
                child_view = e;
        const forge::ObjectTransform transformed(child_view);
        camera.target = transformed.position;
        camera.pitch = -.55f;
        camera.yaw = -.6f;
        camera.distance = 8;
        ++generation;
        const auto sheared = readback(device, context,
                                      viewport.render(context, effective, width, height, camera,
                                                      generation, false, {false, 1}));
        save(sheared, width, height, images / "hierarchical-shear.ppm");
        const auto inv = forge::inverse(transformed.affine);
        const auto eye = camera.eye();
        unsigned lit_faces = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                forge::Float3 local{};
                local[axis] = .5f * sign;
                const auto center = transformed.point(local);
                forge::Float3 normal{};
                for (unsigned j = 0; j < 3; ++j)
                    normal[j] = float(inv.m[4 * axis + j]) * sign;
                const float length = std::sqrt(forge::geom_dot(normal, normal));
                for (auto& value : normal)
                    value /= length;
                const auto to_eye = forge::geom_sub(eye, center);
                if (forge::geom_dot(normal, to_eye) < 1)
                    continue;
                const auto at = forge::project_point(camera, center, width, height);
                require(bool(at), "Hierarchical face outside view");
                auto ray = forge::geom_sub(center, eye);
                const float distance = std::sqrt(forge::geom_dot(ray, ray));
                for (auto& value : ray)
                    value /= distance;
                auto hit = forge::object_hit(child_view, eye, ray, .01f, 1000);
                require(hit && std::abs(*hit - distance) < .001f,
                        "Hierarchical picking does not agree with rendered face");
                const float light_dot =
                    forge::geom_dot(normal, {-.4f, .8f, -.5f}) / std::sqrt(1.05f);
                const int expected = int(std::round(255 * (.3f + .7f * std::max(0.0f, light_dot))));
                const auto pixel = sheared[int((*at)[1]) * width + int((*at)[0])];
                require(std::abs(int(pixel[0]) - expected) <= 2 &&
                            std::abs(int(pixel[1]) - expected) <= 2 &&
                            std::abs(int(pixel[2]) - expected) <= 2,
                        "Hierarchical affine shading lost inverse-transpose normals");
                ++lit_faces;
            }
        require(lit_faces >= 2, "Hierarchical shading fixture needs two visible faces");
        context->WaitForIdle();
        std::cout << "D3D12 WARP: grid axis alignment, look/pan/orbit/fly/zoom, resize, "
                     "visibility, spacing, thin lines, horizon fade, extent and occlusion passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
