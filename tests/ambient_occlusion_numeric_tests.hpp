#pragma once
// Closed-form geometry for the fixture's two orthographic half-planes. Dense
// midpoint integration uses its own direction lattice; it never reconstructs
// depth, projects samples or uses the production sixteen-sample permutation.
namespace forge::test_occlusion {
inline double dense_half_plane(double receiver_x, double nx, double nz, unsigned side) {
    double sum = 0;
    constexpr double tau = 6.283185307179586476925286766559;
    for (unsigned h = 0; h < side; ++h) {
        const double z = (double(h) + .5) / side;
        const double radial = std::sqrt(1 - z * z), reach = .2 + .8 * z * z;
        for (unsigned a = 0; a < 2 * side; ++a) {
            const double angle = tau * (double(a) + .5) / (2 * side);
            // Independent orthonormal basis: y and (nz,0,-nx), around (nx,0,nz).
            const double x = reach * (nz * radial * std::sin(angle) + nx * z);
            const double y = reach * radial * std::cos(angle);
            const double sample_z = reach * (-nx * radial * std::sin(angle) + nz * z);
            // The nearer half-plane exists only at x>=0 and z=9.7. The receiver
            // lies on z=10. The retained geometric hemisphere excludes z=10.
            if (receiver_x + x < 0 || sample_z <= -.29)
                continue;
            const double distance = std::hypot(x, y, .3);
            const double cosine = (nx * x - nz * .3) / distance;
            if (distance < 1 && cosine > .01 / distance)
                sum += cosine * (1 - distance);
        }
    }
    return sum / (side * side); // 2*cosine-weighted hemisphere average.
}
inline void numeric_reference(const std::vector<test_bloom::Pixel>& actual, unsigned width,
                              unsigned height, double scale_x, double nx, double nz) {
    using test_bloom::check;
    const double length = std::hypot(nx, nz);
    nx /= length;
    nz /= length;
    double gpu = 0, dense = 0, coarse = 0;
    unsigned count = 0;
    // Several lattice rotations and receiver distances average the deliberately
    // bounded production pattern. Stay away from viewport and depth-step pixels.
    for (unsigned x = width / 2 - 13; x < width / 2 - 1; ++x) {
        const double world_x = ((double(x) + .5) * 2 / width - 1) / scale_x;
        const double value = dense_half_plane(world_x, nx, nz, 128);
        dense += value;
        coarse += dense_half_plane(world_x, nx, nz, 64);
        double row = 0;
        for (unsigned y = height / 2 - 4; y < height / 2 + 4; ++y)
            row += 2 * (1 - actual[y * width + x][0]); // Known indirect red is .5.
        gpu += row / 8;
        ++count;
    }
    gpu /= count;
    dense /= count;
    coarse /= count;
    check(std::abs(dense - coarse) < .004, "Dense planar AO reference did not converge");
    check(std::abs(gpu - dense) < .06, "Normal AO spatial mean exceeds dense-reference tolerance");
    std::cout << "Normal AO dense planar reference n=(" << nx << ',' << nz << ") gpu=" << gpu
              << " dense=" << dense << " convergence=" << std::abs(dense - coarse) << '\n';
}
} // namespace forge::test_occlusion
