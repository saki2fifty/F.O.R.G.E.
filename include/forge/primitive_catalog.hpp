#pragma once
#include <cstddef>
namespace forge {
// Append-only blockout kind values. 0–3 retain their original persisted meanings.
// None is explicit because legacy transformed entities without Primitive mean Cube.
inline constexpr const char* primitive_names[] = {"Cube",
                                                  "Sphere",
                                                  "Cylinder",
                                                  "Plane",
                                                  "None",
                                                  "Capsule",
                                                  "Cone",
                                                  "Frustum",
                                                  "Quad",
                                                  "Disc",
                                                  "Ring",
                                                  "Torus",
                                                  "Pyramid",
                                                  "Tetrahedron",
                                                  "Octahedron",
                                                  "Triangular Prism",
                                                  "Hexagonal Prism",
                                                  "Wedge",
                                                  "Hemisphere",
                                                  "Icosphere",
                                                  "Tube"};
inline constexpr unsigned primitive_count = sizeof(primitive_names) / sizeof(*primitive_names);
inline constexpr unsigned no_primitive = 4;
} // namespace forge
