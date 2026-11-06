#pragma once
#include "../temporal_frame_sample.hpp"
#include "sequence_frame_output.hpp"
#include <filesystem>
#include <forge/scene_identity.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace Diligent {
struct IDeviceContext;
struct ITextureView;
} // namespace Diligent
namespace forge {
class DiligentPresentation;
}
namespace forge::ui {
// A scoped capture workflow owned by ViewImageTask. Native rendering stays in
// the host; workflows may reuse a scoped ViewImageTask for completed PNG writes.
struct ViewImageWork {
    virtual ~ViewImageWork() = default;
    virtual bool busy() const = 0;
    virtual void poll(Diligent::IDeviceContext*) = 0;
    virtual void draw() = 0;
    virtual bool cancel() = 0;
    virtual Json state() const = 0;
    virtual std::string_view kind() const { return "reflection"; }
    virtual std::optional<SequenceFrameExtent> extent() const { return {}; }
    virtual std::optional<TemporalFrameSample> temporal_sample() const { return {}; }
    virtual void game_frame(DiligentPresentation&, Diligent::IDeviceContext*,
                            Diligent::ITextureView*, bool) {}
    virtual std::filesystem::path preview_path(std::optional<unsigned>) const {
        throw std::runtime_error("This capture has no completed PNG frame preview");
    }
};
} // namespace forge::ui
