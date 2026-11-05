#include "ui_luminance_mask.hpp"
#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/StyleSheetTypes.h>
#include <array>
#include <stdexcept>
#include <string_view>

namespace forge {
namespace {
class LuminanceMask final : public Rml::Decorator {
    Rml::SharedPtr<Rml::Decorator> source_;
    Rml::BoxArea area_;
    struct Data {
        Rml::SharedPtr<Rml::Decorator> source;
        Rml::DecoratorDataHandle pixels = 0;
        Rml::CompiledFilter filter;
        ~Data() {
            if (pixels)
                source->ReleaseElementData(pixels);
        }
    };

  public:
    LuminanceMask(Rml::SharedPtr<Rml::Decorator> source, Rml::BoxArea area)
        : source_(std::move(source)), area_(area) {}
    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element,
                                                 Rml::BoxArea area) const override {
        auto data = std::make_unique<Data>();
        data->source = source_;
        data->pixels =
            source_->GenerateElementData(element, area_ == Rml::BoxArea::Auto ? area : area_);
        if (!data->pixels)
            return 0;
        data->filter = element->GetRenderManager()->CompileFilter("forge-luminance-mask", {});
        if (!data->filter)
            return 0;
        return reinterpret_cast<Rml::DecoratorDataHandle>(data.release());
    }
    void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
        delete reinterpret_cast<Data*>(handle);
    }
    void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
        auto& data = *reinterpret_cast<Data*>(handle);
        auto& render = *element->GetRenderManager();
        Rml::FilterHandleList filters;
        data.filter.AddHandleTo(filters);
        const auto destination = render.GetTopLayer();
        const auto layer = render.PushLayer();
        try {
            source_->RenderElement(element, data.pixels);
            render.CompositeLayers(layer, destination, Rml::BlendMode::Blend, filters);
        } catch (...) {
            render.PopLayer();
            throw;
        }
        render.PopLayer();
    }
};
class LuminanceInstancer final : public Rml::DecoratorInstancer {
    Rml::PropertyId source_;

  public:
    LuminanceInstancer() {
        source_ = RegisterProperty("source", "none").AddParser("string").GetId();
        RegisterShorthand("decorator", "source", Rml::ShorthandType::RecursiveRepeat);
    }
    Rml::SharedPtr<Rml::Decorator>
    InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary& properties,
                      const Rml::DecoratorInstancerInterface& interface) override {
        const auto* property = properties.GetProperty(source_);
        if (!property || property->value.GetType() != Rml::Variant::STRING)
            throw std::runtime_error("UI luminance needs one anonymous image or gradient");
        const auto source = property->Get<Rml::String>();
        if (source.empty() || source.size() > 4096)
            throw std::runtime_error("UI luminance source must contain 1..4096 bytes");
        // The native parser produces declarations without instancing them.
        // Validate before delegation so nested wrappers cannot recurse.
        Rml::PropertyDictionary parsed;
        if (!Rml::StyleSheetSpecification::ParsePropertyDeclaration(parsed, "mask-image", source))
            throw std::runtime_error("UI luminance source is not a native decorator declaration");
        const auto* value = parsed.GetProperty(Rml::PropertyId::MaskImage);
        const auto declarations = value ? value->Get<Rml::DecoratorsPtr>() : nullptr;
        if (!declarations || declarations->list.size() != 1)
            throw std::runtime_error("UI luminance needs exactly one image or gradient");
        const auto& declaration = declarations->list.front();
        constexpr std::array<std::string_view, 10> supported{"image",
                                                             "linear-gradient",
                                                             "repeating-linear-gradient",
                                                             "radial-gradient",
                                                             "repeating-radial-gradient",
                                                             "conic-gradient",
                                                             "repeating-conic-gradient",
                                                             "horizontal-gradient",
                                                             "vertical-gradient",
                                                             "gradient"};
        bool allowed = false;
        for (const auto type : supported)
            allowed |= declaration.type == type;
        if (!allowed || !declaration.instancer)
            throw std::runtime_error("UI luminance supports one anonymous image or gradient; "
                                     "names, lists and nested wrappers are unavailable");
        auto inner = declaration.instancer->InstanceDecorator(declaration.type,
                                                              declaration.properties, interface);
        if (!inner)
            throw std::runtime_error("UI luminance source could not be instanced");
        return Rml::MakeShared<LuminanceMask>(std::move(inner), declaration.paint_area);
    }
};
} // namespace
std::unique_ptr<Rml::DecoratorInstancer> register_ui_luminance_mask() {
    auto instance = std::make_unique<LuminanceInstancer>();
    Rml::Factory::RegisterDecoratorInstancer("luminance", instance.get());
    return instance;
}
} // namespace forge
