#pragma once
#include <memory>

namespace Rml {
class DecoratorInstancer;
}
namespace forge {
// Lifetime belongs to the presenter: create after native initialization and retain
// until all native contexts and the global factory have shut down.
std::unique_ptr<Rml::DecoratorInstancer> register_ui_luminance_mask();
} // namespace forge
