#include "mesh_occlusion_plan.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    using namespace forge;
    try {
        const auto single = mesh_occlusion_image_plan(1, 1, 16384);
        check(single && *single == MeshOcclusionImagePlan{1, 1, 1, 1, 1, 4, 4},
              "Single-pixel hierarchy did not retain a valid root");
        // 3x5 input -> 2x4 padded half-resolution root -> 1x2 -> 1x1.
        // These explicit footprints catch lost odd rows/columns and mip tails.
        const auto odd = mesh_occlusion_image_plan(3, 5, 16384);
        check(odd && *odd == MeshOcclusionImagePlan{3, 5, 2, 4, 3, 60, 44},
              "Odd viewport hierarchy dimensions or byte accounting differ");
        const auto uhd = mesh_occlusion_image_plan(3840, 2160, 16384);
        check(uhd && uhd->pyramid_width == 2048 && uhd->pyramid_height == 2048 &&
                  uhd->levels == 12 && uhd->depth_bytes == 33177600 &&
                  uhd->pyramid_bytes == 22369620,
              "4K occlusion image plan exceeded its documented footprint");
        const auto limit = mesh_occlusion_image_plan(4096, 2048, 16384);
        check(limit && limit->depth_bytes == 33554432,
              "Exact occlusion input pixel limit was rejected");
        for (const auto& extent : {std::pair{0u, 1u},
                                   {1u, 0u},
                                   {4097u, 2048u},
                                   {16385u, 1u},
                                   {1u, 16385u},
                                   {std::numeric_limits<unsigned>::max(), 1u},
                                   {1u, std::numeric_limits<unsigned>::max()}})
            check(!mesh_occlusion_image_plan(extent.first, extent.second, 16384),
                  "Unsupported occlusion extent did not request ordinary rendering");
        check(!mesh_occlusion_image_plan(3840, 2160, 2048) && !mesh_occlusion_image_plan(1, 1, 0),
              "Occlusion planning ignored the actual device dimension limit");
        for (unsigned width = 1; width < 80; ++width)
            for (unsigned height = 1; height < 80; ++height) {
                const auto plan = mesh_occlusion_image_plan(width, height, 16384);
                check(plan && plan->pyramid_width * 2 >= width &&
                          plan->pyramid_height * 2 >= height &&
                          std::has_single_bit(plan->pyramid_width) &&
                          std::has_single_bit(plan->pyramid_height) &&
                          plan->depth_bytes + plan->pyramid_bytes <= mesh_occlusion_image_budget,
                      "Small odd/asymmetric hierarchy lost coverage or exceeded its budget");
            }
        std::cout << "Occlusion image accounting, odd padding, exact budget and fail-open limits "
                     "passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
