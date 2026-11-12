#pragma once
#include "../src/ui_filter_admission.hpp"
#include "../src/ui_gradient_admission.hpp"
#include "../src/ui_material_effect.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <forge/ui_presenter.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
struct UiTestRenderer final : Rml::RenderInterface {
    static void check(bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    }
    std::set<std::uintptr_t> geometry, textures;
    std::uintptr_t next = 1;
    unsigned draws = 0, clips = 0, transforms = 0;
    bool filters_supported = false, fail_layer = false;
    bool gradients_supported = false, masks_supported = false, snapshots_supported = false;
    unsigned snapshot_saves = 0;
    std::set<std::uintptr_t> snapshot_textures;
    unsigned mask_saves = 0;
    unsigned gradient_draws = 0;
    std::map<Rml::CompiledShaderHandle, forge::ui_gradient_detail::Gradient> gradients;
    bool effects_supported = false;
    unsigned effect_draws = 0;
    bool fail_shader = false;
    std::map<Rml::CompiledShaderHandle, forge::ui_effect_detail::MaterialEffect> effects;
    Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override {
        if (name == "shader") {
            if (!effects_supported)
                return 0;
            const auto value = forge::ui_effect_detail::admit_material_effect(parameters);
            check(gradients.size() + effects.size() < forge::ui_gradient_detail::max_shaders,
                  "Bounded compiled shader count");
            const auto id = next++;
            effects.emplace(id, value);
            return id;
        }
        if (!gradients_supported)
            return 0;
        const auto value = forge::ui_gradient_detail::admit_gradient(name, parameters);
        check(gradients.size() + effects.size() < forge::ui_gradient_detail::max_shaders,
              "Bounded compiled shader count");
        const auto id = next++;
        gradients.emplace(id, value);
        return id;
    }
    void ReleaseShader(Rml::CompiledShaderHandle shader) override {
        if (effects.erase(shader))
            return;
        check(gradients.erase(shader) == 1, "Gradient retired once");
    }
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle h,
                      Rml::Vector2f, Rml::TextureHandle texture) override {
        check(!fail_shader, "fixture.ui.shader.realization");
        if (effects.contains(shader)) {
            check(geometry.contains(h) && !texture, "Live native UI effect geometry");
            ++effect_draws;
            return;
        }
        check(gradients.contains(shader) && geometry.contains(h) && !texture,
              "Live native gradient geometry");
        ++gradient_draws;
    }
    unsigned layer_pushes = 0, composites = 0;
    std::map<Rml::CompiledFilterHandle, std::pair<std::string, float>> filters;
    std::vector<Rml::LayerHandle> layers;
    std::set<std::string> loaded_sources;
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                Rml::Span<const int> i) override {
        check(!v.empty() && !i.empty(), "Geometry data");
        auto id = next++;
        geometry.insert(id);
        return id;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle g, Rml::Vector2f,
                        Rml::TextureHandle t) override {
        check(geometry.contains(g), "Live geometry");
        check(!t || textures.contains(t), "Live texture");
        ++draws;
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle g) override {
        check(geometry.erase(g) == 1, "Geometry retired once");
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String& path) override {
        Rml::String bytes;
        if (!Rml::GetFileInterface()->LoadFile(path, bytes))
            return 0;
        loaded_sources.insert(path);
        auto image = forge::decode_ui_image(
            {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
        dims = {int(image.width), int(image.height)};
        return GenerateTexture(
            {reinterpret_cast<const Rml::byte*>(image.rgba.data()), image.rgba.size()}, dims);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> bytes,
                                       Rml::Vector2i dims) override {
        check(bytes.size() == std::size_t(dims.x) * dims.y * 4, "Texture dimensions");
        auto id = next++;
        textures.insert(id);
        return id;
    }
    void ReleaseTexture(Rml::TextureHandle t) override {
        check(textures.erase(t) == 1, "Texture retired once");
        snapshot_textures.erase(t);
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override {
        if (!filters_supported)
            return 0;
        const auto value = forge::ui_filter_detail::admit_filter(name, parameters).value;
        const auto handle = next++;
        filters.emplace(handle, std::pair{name, value});
        return handle;
    }
    void ReleaseFilter(Rml::CompiledFilterHandle handle) override {
        check(filters.erase(handle) == 1, "Filter retired once");
    }
    Rml::TextureHandle SaveLayerAsTexture() override {
        if (!snapshots_supported)
            return 0;
        check(!layers.empty() && textures.size() < 512, "Bounded live snapshot layer");
        const auto handle = next++;
        textures.insert(handle);
        snapshot_textures.insert(handle);
        ++snapshot_saves;
        return handle;
    }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
        check(masks_supported && !layers.empty(), "Supported live mask layer");
        check(filters.size() < forge::ui_filter_detail::max_filters, "Bounded mask filter count");
        const auto handle = next++;
        filters.emplace(handle, std::pair{std::string("alpha-mask"), 1.f});
        ++mask_saves;
        return handle;
    }
    Rml::LayerHandle PushLayer() override {
        if (fail_layer)
            throw std::runtime_error("fixture.filter.layer-allocation");
        check(filters_supported, "Unsupported recording filter layer");
        const auto handle = next++;
        layers.push_back(handle);
        ++layer_pushes;
        return handle;
    }
    void PopLayer() override {
        check(!layers.empty(), "Native filter layer walk balanced");
        layers.pop_back();
    }
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode,
                         Rml::Span<const Rml::CompiledFilterHandle> chain) override {
        const auto live = [&](Rml::LayerHandle h) {
            return !h || std::find(layers.begin(), layers.end(), h) != layers.end();
        };
        check(live(source) && live(destination), "Live filter composition layers");
        for (const auto handle : chain)
            check(filters.contains(handle), "Live compiled filter");
        ++composites;
    }
    void EnableScissorRegion(bool) override { ++clips; }
    void SetScissorRegion(Rml::Rectanglei) override { ++clips; }
    void SetTransform(const Rml::Matrix4f*) override { ++transforms; }
    void EnableClipMask(bool) override {}
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle,
                          Rml::Vector2f) override {}
};
