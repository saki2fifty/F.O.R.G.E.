#pragma once
#include "runtime_reflection_gpu.hpp"
#include "temporal_frame_sample.hpp"
#include <chrono>
#include <optional>
namespace forge {
class FrameRenderer;
class MeshResourceHost;
class MeshSceneRenderer;
struct RenderScene;
// Per-view derived passes. Existing residency owns all native payloads/fences;
// the extracted Scene owns probe intent and simulation owns refresh time.
class RuntimeReflectionCaptures {
  public:
    RuntimeReflectionCaptures(DiligentPresentation&, std::shared_ptr<MeshResourceHost>);
    ~RuntimeReflectionCaptures();
    const RuntimeReflectionImages& render(Diligent::IDeviceContext*, const RenderScene&,
                                          MeshSceneRenderer&, double seconds,
                                          const TemporalFrameSample*);
    void invalidate();
    void submit();
    bool pending() const { return pending_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  private:
    struct Configuration {
        unsigned size = 128;
        Double3 center{};
        double near_plane = .1, far_plane = 100;
        std::uint32_t layers = UINT32_MAX;
        bool operator==(const Configuration&) const = default;
    };
    struct Target {
        Configuration configuration;
        std::uint64_t generation = 0;
        RuntimeReflectionLease ready, staging;
        std::optional<double> completed, attempted;
        std::chrono::steady_clock::time_point deadline{};
        std::string error;
    };
    DiligentPresentation& presentation_;
    std::shared_ptr<MeshResourceHost> host_;
    RuntimeReflectionResidency gpu_;
    std::unique_ptr<FrameRenderer> renderer_;
    RuntimeReflectionImages images_;
    std::map<EntityId, Target> targets_;
    std::vector<Diagnostic> diagnostics_;
    AssetId scene_, operation_;
    std::uint64_t epoch_ = 0, generation_ = 0;
    std::optional<double> time_;
    bool pending_ = false, simulated_ = false;
    void release(Target&);
    void report(AssetId, EntityId, const std::string&);
};
} // namespace forge
