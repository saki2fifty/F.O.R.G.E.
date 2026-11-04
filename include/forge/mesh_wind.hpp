#pragma once
#include <cmath>
#include <stdexcept>
namespace forge {
inline constexpr unsigned mesh_wind_profile_version = 1;
// Geometry intent in world meters. Rendering borrows time; this value owns no clock.
struct MeshWind {
    bool enabled = false;
    double amplitude = .2, height = 1.6;
    double direction_x = 1, direction_z = 0;
    double frequency = .5, wavelength = 8, phase = 0;
    bool operator==(const MeshWind&) const = default;
};
inline void validate_mesh_wind(const MeshWind& value) {
    const auto range = [](double number, double low, double high, const char* message) {
        if (!std::isfinite(number) || number < low || number > high)
            throw std::runtime_error(message);
    };
    range(value.amplitude, 0, 100, "Mesh wind amplitude must be 0 to 100 meters");
    range(value.height, .01, 10000, "Mesh wind height must be 0.01 to 10000 meters");
    range(value.direction_x, -1e6, 1e6, "Mesh wind X direction must be finite within +/-1 million");
    range(value.direction_z, -1e6, 1e6, "Mesh wind Z direction must be finite within +/-1 million");
    range(value.frequency, 0, 60, "Mesh wind frequency must be 0 to 60 cycles per second");
    range(value.wavelength, .1, 10000, "Mesh wind wavelength must be 0.1 to 10000 meters");
    range(value.phase, -360, 360, "Mesh wind phase must be -360 to 360 degrees");
    if (value.enabled && value.amplitude != 0 && value.direction_x == 0 && value.direction_z == 0)
        throw std::runtime_error("Enabled mesh wind requires a nonzero horizontal direction");
}
} // namespace forge
