#include "ui_shadow_motion.hpp"
#include "ui_filter_profile.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <cmath>
#include <stdexcept>

namespace forge {
namespace {
using PropertyIds = std::array<Rml::PropertyId, ui_shadow_detail::properties.size()>;
class ShadowPanel final : public Rml::Element {
    PropertyIds ids_;

  public:
    ShadowPanel(const Rml::String& tag, const PropertyIds& ids) : Rml::Element(tag), ids_(ids) {}
    void synchronize() {
        const auto length = [&](unsigned index) {
            const auto* property = GetProperty(ids_[index]);
            if (!property)
                throw std::runtime_error("Shadow panel property is unavailable");
            const auto numeric = property->GetNumericValue();
            if (!Rml::Any(numeric.unit & (Rml::Unit::LENGTH | Rml::Unit::NUMBER)))
                throw std::runtime_error("Shadow panel lengths require supported length units");
            const float value = ResolveLength(numeric);
            if (!std::isfinite(value) || std::abs(value) > 8192)
                throw std::runtime_error("Shadow panel lengths must resolve within +/-8192 pixels");
            return Rml::NumericValue{value, Rml::Unit::PX};
        };
        Rml::BoxShadow value;
        value.offset_x = length(0);
        value.offset_y = length(1);
        value.blur_radius = length(2);
        value.spread_distance = length(3);
        if (value.blur_radius.number < 0 || value.blur_radius.number > 32)
            throw std::runtime_error("Shadow panel blur must resolve between 0 and 32 pixels");
        value.color = GetProperty(ids_[4])->Get<Rml::Colourb>().ToPremultiplied();
        const auto inset = GetProperty(ids_[5])->Get<int>();
        if (inset != 0 && inset != 1)
            throw std::runtime_error("Shadow panel inset must be outset or inset");
        value.inset = inset == 1;
        const auto& current =
            GetProperty(Rml::PropertyId::BoxShadow)->value.GetReference<Rml::BoxShadowList>();
        if (current.size() == 1 && current.front() == value)
            return;
        if (!SetProperty(Rml::PropertyId::BoxShadow,
                         Rml::Property(Rml::BoxShadowList{value}, Rml::Unit::BOXSHADOWLIST)))
            throw std::runtime_error("Shadow panel could not update its native shadow");
        const auto dimensions = GetContext()->GetDimensions();
        UpdateProperties(GetContext()->GetDensityIndependentPixelRatio(),
                         {float(dimensions.x), float(dimensions.y)});
    }
};
class ShadowInstancer final : public Rml::ElementInstancer {
    PropertyIds ids_;

  public:
    ShadowInstancer() {
        for (unsigned i = 0; i < ids_.size(); ++i) {
            const bool color = i == 4, inset = i == 5;
            auto& definition = Rml::StyleSheetSpecification::RegisterProperty(
                Rml::String(ui_shadow_detail::properties[i]),
                color   ? "#0000"
                : inset ? "outset"
                        : "0px",
                false, false);
            definition.AddParser(color   ? "color"
                                 : inset ? "keyword"
                                         : "length",
                                 inset ? "outset,inset" : "");
            ids_[i] = definition.GetId();
            if (ids_[i] == Rml::PropertyId::Invalid)
                throw std::runtime_error("Shadow panel style registration failed");
        }
    }
    Rml::ElementPtr InstanceElement(Rml::Element*, const Rml::String& tag,
                                    const Rml::XMLAttributes&) override {
        return Rml::ElementPtr(new ShadowPanel(tag, ids_));
    }
    void ReleaseElement(Rml::Element* element) override { delete element; }
};
} // namespace
std::unique_ptr<Rml::ElementInstancer> register_ui_shadow_motion() {
    auto instancer = std::make_unique<ShadowInstancer>();
    Rml::Factory::RegisterElementInstancer(Rml::String(ui_shadow_detail::tag), instancer.get());
    return instancer;
}
void synchronize_ui_shadow_motion(Rml::Context& context) {
    Rml::ElementList elements;
    context.GetRootElement()->GetElementsByTagName(elements, Rml::String(ui_shadow_detail::tag));
    if (elements.size() > ui_filter_detail::max_saved_textures)
        throw std::runtime_error("UI view exceeds 24 shadow panels; reduce animated shadows");
    for (auto* element : elements) {
        auto* panel = dynamic_cast<ShadowPanel*>(element);
        if (!panel)
            throw std::runtime_error("Shadow panel native type is unavailable");
        panel->synchronize();
    }
}
} // namespace forge
