#pragma once
#include <RmlUi/Core.h>
#include <forge/ui_presenter.hpp>
#include <set>
#include <stdexcept>
struct UiTestRenderer final : Rml::RenderInterface {
    static void check(bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    }
    std::set<std::uintptr_t> geometry, textures;
    std::uintptr_t next = 1;
    unsigned draws = 0, clips = 0, transforms = 0;
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
    }
    void EnableScissorRegion(bool) override { ++clips; }
    void SetScissorRegion(Rml::Rectanglei) override { ++clips; }
    void SetTransform(const Rml::Matrix4f*) override { ++transforms; }
    void EnableClipMask(bool) override {}
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle,
                          Rml::Vector2f) override {}
};
