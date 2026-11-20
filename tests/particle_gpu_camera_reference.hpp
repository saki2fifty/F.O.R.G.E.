#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "particle_gpu.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace forge::particle_gpu_test {
inline std::array<ParticleGpuRow, 7> billboard_reference(std::array<ParticleGpuRow, 7> rows,
                                                         const CameraView& camera) {
    const double width = std::hypot(double(rows[0][0]), double(rows[1][0]), double(rows[2][0]));
    const double height = std::hypot(double(rows[0][1]), double(rows[1][1]), double(rows[2][1]));
    double maximum = 0;
    for (unsigned r = 0; r < 3; ++r) {
        rows[r][0] = float(camera.right[r] * width);
        rows[r][1] = float(camera.up[r] * height);
        rows[r][2] = float(-camera.forward[r] * std::max(width, height));
        for (unsigned c = 0; c < 3; ++c)
            maximum = std::max(maximum, std::abs(double(rows[r][c])));
    }
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c)
            rows[r + 3][c] = maximum > 0 ? float(rows[r][c] / maximum) : 0;
        rows[r + 3][3] = 0;
    }
    rows[6][3] = 2;
    return rows;
}
std::vector<std::array<ParticleGpuRow, 7>> read_particles(Diligent::IDeviceContext* context,
                                                          const ParticleGpuInstances& particles,
                                                          Diligent::IBuffer* staging) {
    std::vector<std::array<ParticleGpuRow, 7>> result(particles.count());
    if (result.empty())
        return result;
    const auto bytes = result.size() * sizeof(result.front());
    context->CopyBuffer(particles.buffer(), 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                        staging, 0, bytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    Diligent::MapHelper<ParticleGpuRow> mapped(context, staging, Diligent::MAP_READ,
                                               Diligent::MAP_FLAG_DO_NOT_WAIT);
    if (!static_cast<ParticleGpuRow*>(mapped))
        throw std::runtime_error("Camera projection map failed");
    for (std::size_t id = 0; id < result.size(); ++id)
        for (unsigned row = 0; row < 7; ++row)
            result[id][row] = mapped[id * 7 + row];
    return result;
}
} // namespace forge::particle_gpu_test
