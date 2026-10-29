#pragma once
#include <forge/scene_identity.hpp>
#include <string>
namespace Diligent {
struct IDeviceContext;
}
namespace forge::ui {
// A scoped non-PNG operation owned by ViewImageTask. Native rendering stays in
// the host; editor-only hosts can exercise authoring without linking a GPU.
struct ViewImageWork {
    virtual ~ViewImageWork() = default;
    virtual bool busy() const = 0;
    virtual void poll(Diligent::IDeviceContext*) = 0;
    virtual void draw() = 0;
    virtual bool cancel() = 0;
    virtual Json state() const = 0;
};
} // namespace forge::ui
