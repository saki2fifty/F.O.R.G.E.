#pragma once
#include "environment_gpu.hpp"
#include <forge/reflection_probe.hpp>
namespace forge {
// Private view-local configuration, never an asset type or CPU resource pool.
struct RuntimeReflectionMap {};
template <> struct ResourceTraits<RuntimeReflectionMap> {
    using Data = unsigned;
};
struct RuntimeReflectionKey {
    EntityId entity;
    std::uint64_t generation = 0;
    unsigned slot = 0;
    auto operator<=>(const RuntimeReflectionKey&) const = default;
};
struct RuntimeReflectionRealization {
    using Data = GpuEnvironment;
    DiligentPresentation* presentation = nullptr;
    std::uint64_t bytes(unsigned size) const;
    Data upload(Diligent::IRenderDevice*, Diligent::IDeviceContext*, unsigned size) const;
};
using RuntimeReflectionLease =
    GpuLease<RuntimeReflectionMap, RuntimeReflectionRealization, RuntimeReflectionKey>;
using RuntimeReflectionImages = std::map<EntityId, RuntimeReflectionLease>;
using RuntimeReflectionResidency =
    GpuResidency<RuntimeReflectionMap, RuntimeReflectionRealization, RuntimeReflectionKey>;
} // namespace forge
