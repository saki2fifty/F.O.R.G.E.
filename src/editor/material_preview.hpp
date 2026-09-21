#pragma once
#include "camera.hpp"
#include "frame_renderer.hpp"
#include "property_drawer.hpp"
#include <forge/engine_assets.hpp>
namespace forge {
// Detached preview values and private resource owners; no authored Scene/Flecs
// world, catalog edits, temporary project materials or second rendering backend.
class MaterialPreview {
  public:
    MaterialPreview(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& project,
                    std::shared_ptr<const AssetCatalog> catalog)
        : context_(context),
          host_(std::make_shared<MeshResourceHost>(presentation, context, project, true)),
          frame_(presentation) {
        host_->catalog(std::move(catalog));
        frame_.resources(host_);
        camera.target = {0, 0, 0};
        camera.yaw = -.5f;
        camera.pitch = -.25f;

        scene_.scene = AssetId::generate();
        scene_.settings.shadows.enabled = false;
        object_.entity = EntityId::generate();
        object_.renderer.cast_shadows = false;
        object_.renderer.receive_shadows = false;
        camera_id_ = EntityId::generate();
        light_id_ = EntityId::generate();
    }
    EditorCamera camera;
    unsigned shape = 1; // Existing engine primitive catalog: sphere, cube or plane.
    float exposure = 0, light_intensity = 3;
    std::array<float, 3> background{.025f, .03f, .04f};
    SceneEnvironment environment;
    void catalog(std::shared_ptr<const AssetCatalog> value) { host_->catalog(std::move(value)); }
    void material(AssetRef<MaterialAsset> ref, MaterialResourceData data) {
        host_->preview_material(ref, std::move(data));
        object_.renderer.materials = {{"surface", ref}};
    }
    Diligent::ITextureView* render(unsigned width, unsigned height) {
        if (shape != framed_shape_ ||
            (auto_frame_ && (width != framed_width_ || height != framed_height_))) {
            double radius = 0;
            for (const auto& vertex : primitive_meshes().at(shape))
                radius = std::max(radius, double(std::hypot(vertex.position[0], vertex.position[1],
                                                            vertex.position[2])));
            if (!height || !camera.frame_sphere({0, 0, 0}, radius, float(width) / height))
                throw std::runtime_error("Material preview framing dimensions are invalid");
            framed_shape_ = shape;
            framed_width_ = width;
            framed_height_ = height;
            auto_frame_ = true;
        }
        object_.renderer.mesh = engine_primitive(shape);
        scene_.meshes = {object_};
        scene_.settings.environment = environment;
        Light light;
        light.intensity = light_intensity;
        auto view = light_view(light, {});
        view.direction = {.4, -.8, .4};
        const double length = std::sqrt(.16 + .64 + .16);
        for (auto& c : view.direction)
            c /= length;
        scene_.lights = {{light_id_, view}};
        Camera lens;
        lens.background_r = background[0];
        lens.background_g = background[1];
        lens.background_b = background[2];
        lens.far_plane = 100;
        const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
                   forward = camera.forward();
        AffineTransform world;
        for (unsigned axis = 0; axis < 3; ++axis) {
            world.m[axis * 4] = right[axis];
            world.m[axis * 4 + 1] = up[axis];
            world.m[axis * 4 + 2] = forward[axis];
            world.m[axis * 4 + 3] = eye[axis];
        }
        const std::array cameras{
            PreparedCamera{camera_id_, lens, camera_view(lens, world, width, height)}};
        auto* output = frame_.render(context_, scene_, cameras, width, height, exposure);
        host_->submit();
        return output;
    }
    bool pending() const { return frame_.pending(); }
    const std::vector<Diagnostic>& diagnostics() const { return frame_.diagnostics(); }
    void draw(const AssetCatalog& catalog) {
        ui::heading("Preview", "Unsaved material preview uses isolated resources and the same "
                               "renderer as Scene and Game. It never edits scene content.");
        int kind = shape == 0 ? 1 : shape == 3 ? 2 : 0;
        ui::property_label_row("Geometry",
                               "Select the geometry used only for this material preview.");
        if (ImGui::Combo("##Geometry", &kind, "Sphere\0Cube\0Plane\0"))
            shape = kind == 0 ? 1 : kind == 1 ? 0 : 3;
        ui::help("Select engine preview geometry with the same UV/tangent streams used by authored "
                 "meshes.");
        ui::next_text_button("Frame view");
        if (ui::button("Frame view", "Fit the preview geometry to this image. Restores automatic "
                                     "fitting after manual zoom.")) {
            auto_frame_ = true;
            framed_width_ = 0;
        }
        if (ImGui::TreeNode("Preview lighting and background")) {
            ui::help("Personal preview controls; these do not become authored scene or material "
                     "values.");
            ImGui::SliderFloat("Exposure (stops)", &exposure, -10, 10);
            ui::help("Display exposure in stops, after shared HDR rendering.");
            ImGui::SliderFloat("Key light (lux)", &light_intensity, 0, 20);
            ui::help("Directional preview light intensity in lux.");
            ImGui::ColorEdit3("Background", background.data(), ImGuiColorEditFlags_Float);
            ui::help("Preview camera background color.");
            Json texture = environment.texture.id ? Json(environment.texture.id) : Json();
            if (asset_ref_picker(catalog, texture, "texture", "Environment"))
                environment.texture = texture.is_null() ? AssetRef<TextureAsset>{}
                                                        : texture.get<AssetRef<TextureAsset>>();
            ImGui::SliderFloat("Environment intensity", &environment.intensity, 0, 10);
            ui::help("Scale preview environment lighting without changing the Texture asset.");
            ImGui::Checkbox("Environment background", &environment.sky);
            ui::help("Show the selected environment as the preview sky; disable to use the flat "
                     "background.");
            ImGui::TreePop();
        } else
            ui::help(
                "Expand preview-only exposure, lighting, environment and background settings.");
        const ImVec2 size{std::max(32.f, std::min(ImGui::GetContentRegionAvail().x, 1024.f)),
                          300 * ui::interface_scale};
        auto* texture = render(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y));
        ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(texture)}, size);
        const bool hovered = ImGui::IsItemHovered();
        ui::help("Hold middle mouse to orbit. Scroll to zoom. This camera belongs only to the "
                 "material preview.");
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            orbiting_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            orbiting_ = false;
        if (orbiting_)
            camera.orbit(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
        if (hovered && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0) {
            auto_frame_ = false;
            camera.zoom(ImGui::GetIO().MouseWheel);
        }
        if (pending()) {
            ImGui::TextUnformatted("Preparing preview resources...");
            ui::help("The last complete preview remains usable while new resources are prepared.");
        }
        for (const auto& diagnostic : diagnostics())
            ui::field_error(diagnostic.text);
    }

  private:
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::shared_ptr<MeshResourceHost> host_;
    FrameRenderer frame_;
    RenderScene scene_;
    RenderMesh object_;
    EntityId camera_id_, light_id_;
    bool orbiting_ = false, auto_frame_ = true;
    unsigned framed_shape_ = no_primitive, framed_width_ = 0, framed_height_ = 0;
};
} // namespace forge
