#pragma once
#include "editor/authoring.hpp"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <fstream>

namespace forge::test_orthographic_render {
using Pixels = test_sprite_draw::Pixels;
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
inline void save(const Pixels& pixels, unsigned width, unsigned height,
                 const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& pixel : pixels)
        out.write(reinterpret_cast<const char*>(pixel.data()), 3);
    check(bool(out.flush()), "Orthographic evidence write failed");
}
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    auto* device = presentation.device();
    forge::Viewport legacy(presentation);
    EngineContext engine;
    Scene scene(engine.world());
    const auto cube =
        authoring_command(scene, "entity.create", {{"kind", 0}}).at("selected").get<std::string>();
    const auto cube_id = EntityId::parse(cube);
    std::uint64_t generation = 1;
    const auto finish = [&] {
        context->FinishFrame();
        device->ReleaseStaleResources();
    };
    const auto render = [&](const Json& document, const EditorCamera& camera, unsigned width,
                            unsigned height, GridSettings grid) {
        presentation.begin_frame();
        auto* image =
            legacy.render(context, document, width, height, camera, generation, false, grid);
        presentation.end_frame(context);
        auto pixels = readback(image->GetTexture());
        finish();
        check(pixels.size() == std::size_t(width) * height, "Invalid orthographic image size");
        return pixels;
    };
    for (unsigned axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1})
            for (const auto size : {std::pair{640u, 400u}, std::pair{400u, 640u}}) {
                const auto [width, height] = size;
                const auto plane = axis == 0   ? GridPlane::YZ
                                   : axis == 1 ? GridPlane::XZ
                                               : GridPlane::XY;
                const unsigned first = axis == 0 ? 1 : 0, second = axis == 2 ? 1 : 2;
                EditorCamera camera;
                camera.target = {0, 0, 0};
                camera.distance = 12;
                camera.set_projection(CameraProjection::Orthographic);
                camera.orthographic_height = 10;
                camera.align(axis, sign);
                LocalTranslation position{};
                float coords[3]{};
                coords[axis] = float(sign);
                coords[first] = 2;
                coords[second] = 1;
                position.x = coords[0];
                position.y = coords[1];
                position.z = coords[2];
                scene.entity(cube_id.str()).set<LocalTranslation>(position);
                const auto source = scene.effective_document();
                ++generation;
                const auto clear = render(source, camera, width, height, {false, 1, plane});
                const float units = float(height) / 10;
                // Independent signed-axis screen coordinates; no editor projection helper.
                const float expected_x = width / 2.f + (axis == 0   ? float(sign)
                                                        : axis == 2 ? -2.f * sign
                                                                    : 2.f) *
                                                           units;
                const float expected_y = height / 2.f - (axis == 0   ? 2.f
                                                         : axis == 1 ? float(sign)
                                                                     : 1.f) *
                                                            units;
                unsigned min_x = width, min_y = height, max_x = 0, max_y = 0, colored = 0;
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width; ++x)
                        if (clear[std::size_t(y) * width + x] != clear[0]) {
                            min_x = std::min(min_x, x);
                            max_x = std::max(max_x, x);
                            min_y = std::min(min_y, y);
                            max_y = std::max(max_y, y);
                            ++colored;
                        }
                check(colored > 500 && std::abs((min_x + max_x + 1) / 2.f - expected_x) < 2 &&
                          std::abs((min_y + max_y + 1) / 2.f - expected_y) < 2 &&
                          std::abs(float(max_x - min_x + 1) - units) < 2 &&
                          std::abs(float(max_y - min_y + 1) - units) < 2,
                      "Legacy orthographic cube pixels disagree with independent axis/size oracle");
                check(legacy.pick(source, camera, width, height, expected_x, expected_y) == cube &&
                          legacy.pick(source, camera, width, height, width * .05, height * .05)
                              .empty(),
                      "Orthographic pixels and actual viewport picking disagree");
                const auto with_grid = render(source, camera, width, height, {true, 1, plane});
                const auto empty_source = [&] {
                    auto value = source;
                    value["entities"] = Json::array();
                    return value;
                }();
                for (int dy = -4; dy <= 4; ++dy)
                    for (int dx = -4; dx <= 4; ++dx) {
                        const auto index =
                            std::size_t(int(expected_y) + dy) * width + int(expected_x) + dx;
                        check(with_grid[index] == clear[index],
                              "Grid depth failed to respect opaque orthographic geometry");
                    }
                ++generation;
                const auto grid_image =
                    render(empty_source, camera, width, height, {true, 1, plane});
                check(grid_image != with_grid, "Grid-only fixture retained stale cube pixels");
                for (unsigned selected : {first, second}) {
                    // Derive known projected axis direction from the signed-axis oracle.
                    float x = width / 2.f, y = height / 2.f;
                    if (axis == 0) {
                        if (selected == 1)
                            y -= 2 * units;
                        else
                            x += sign * 2 * units;
                    }
                    if (axis == 1) {
                        if (selected == 0)
                            x += 2 * units;
                        else
                            y -= sign * 2 * units;
                    }
                    if (axis == 2) {
                        if (selected == 0)
                            x -= sign * 2 * units;
                        else
                            y -= 2 * units;
                    }
                    bool found = false;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const auto pixel =
                                grid_image[std::size_t(int(y) + dy) * width + int(x) + dx];
                            found |= pixel[selected] > 45 &&
                                     pixel[selected] > pixel[(selected + 1) % 3] * 1.5 &&
                                     pixel[selected] > pixel[(selected + 2) % 3] * 1.5;
                        }
                    check(found, "Orthographic grid axis color/position drifted");
                }
                const auto retained_before = legacy.retained;
                check(render(empty_source, camera, width, height, {true, 1, plane}) == grid_image &&
                          legacy.retained == retained_before + 1,
                      "Unchanged orthographic view failed to retain its image");
                camera.orthographic_height = 5;
                check(render(empty_source, camera, width, height, {true, 1, plane}) != grid_image,
                      "Changed orthographic extent reused stale native pixels");
                camera.orthographic_height = 10;
                check(render(empty_source, camera, width, height, {true, 2, plane}) != grid_image,
                      "Changed plane spacing reused stale native pixels");
                save(with_grid, width, height,
                     output / ("axes-" + std::to_string(axis) +
                               (sign > 0 ? "-positive-" : "-negative-") + std::to_string(width) +
                               ".ppm"));
            }

    const auto project = output / "project";
    std::filesystem::create_directories(project);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(project));
    const auto material_id = AssetId::generate();
    const AssetRef<MaterialAsset> material{material_id};
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.double_sided = true;
    surface.values.depth_test = surface.values.depth_write = true;
    surface.values.alpha = MaterialAlpha::Opaque;
    auto& slot = surface.values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, surface);
    EngineContext image_engine;
    Scene image_scene(image_engine.world());
    const auto image =
        authoring_command(image_scene, "entity.create",
                          {{"name", "Image projection oracle"}, {"recipe", "render.sprite"}})
            .at("selected")
            .get<std::string>();
    const auto image_id = EntityId::parse(image);
    SpriteRenderer sprite;
    sprite.material = material;
    sprite.width = 1.5;
    sprite.height = 1;
    sprite.columns = sprite.rows = 4;
    image_scene.entity(image).set<SpriteRenderer>(sprite);
    image_scene.entity(image).set<LocalTranslation>({1, .5, -1});
    forge::Viewport scene_view(presentation, true);
    scene_view.resources(host);
    FrameRenderer game(presentation);
    game.resources(host);
    for (const bool orthographic : {false, true})
        for (unsigned direction = 0; direction < 3; ++direction)
            for (const auto size : {std::pair{640u, 400u}, std::pair{400u, 640u}}) {
                const auto [width, height] = size;
                auto value = sprite;
                value.pivot_x = .2f;
                value.pivot_y = .75f;
                value.flip_x = direction == 1;
                value.flip_y = direction == 2;
                image_scene.entity(image).set<SpriteRenderer>(value);
                image_scene.entity(image).set<LocalScale>(
                    {direction == 1 ? -1.f : 1.f, direction == 2 ? -1.f : 1.f, 1});
                const auto document = image_scene.effective_document();
                const auto baseline = image_scene.document();
                EditorCamera camera;
                camera.target = {0, 0, 0};
                camera.distance = 8;
                if (orthographic)
                    camera.set_projection(CameraProjection::Orthographic);
                camera.orthographic_height = 6;
                if (direction == 1)
                    camera.align(2, 1);
                if (direction == 2) {
                    camera.yaw = .4f;
                    camera.pitch = -.2f;
                }
                auto extracted = extract_render_scene(document);
                Camera settings;
                settings.projection = unsigned(camera.projection);
                settings.vertical_fov = 2 * std::atan(1. / EditorCamera::focal);
                settings.orthographic_height = 6;
                settings.near_plane = EditorCamera::near_plane;
                settings.far_plane = EditorCamera::far_plane;
                settings.background_r = .025f;
                settings.background_g = .04f;
                settings.background_b = .055f;
                AffineTransform world;
                const auto right = camera.right(), up = camera.up(), forward = camera.forward(),
                           eye = camera.eye();
                for (unsigned i = 0; i < 3; ++i) {
                    world.m[i * 4] = right[i];
                    world.m[i * 4 + 1] = up[i];
                    world.m[i * 4 + 2] = forward[i];
                    world.m[i * 4 + 3] = eye[i];
                }
                const PreparedCamera prepared{EntityId::generate(), settings,
                                              camera_view(settings, world, width, height)};
                Pixels actual, reference;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                do {
                    presentation.begin_frame();
                    auto* result =
                        scene_view.render(context, document, width, height, camera, ++generation,
                                          false, {false, 1, GridPlane::XY});
                    presentation.end_frame(context);
                    host->submit();
                    actual = readback(result->GetTexture());
                    finish();
                    presentation.begin_frame();
                    auto* expected =
                        game.render(context, extracted, {&prepared, 1}, width, height, 0);
                    presentation.end_frame(context);
                    host->submit();
                    reference = readback(expected->GetTexture());
                    finish();
                    check(std::chrono::steady_clock::now() < deadline,
                          "Orthographic image resources timed out");
                } while (game.pending() || (scene_view.meshes() && scene_view.meshes()->pending()));
                check(actual == reference,
                      "Editor sprite/HDR projection pixels differ from existing Game camera");
                const auto pixel = project_point(camera, {1, .5f, -1}, float(width), float(height));
                check(pixel && scene_view.pick(document, camera, width, height, (*pixel)[0],
                                               (*pixel)[1]) == image_id.str(),
                      "Native sprite projection/reflection/pivot drawing and picking disagree");
                save(actual, width, height,
                     output / (std::string(orthographic ? "sprite-orthographic-"
                                                        : "sprite-perspective-") +
                               std::to_string(direction) + "-" + std::to_string(width) + ".ppm"));
                check(image_scene.document() == baseline,
                      "Editor views mutated authored Sprite content");
            }
}
} // namespace forge::test_orthographic_render
