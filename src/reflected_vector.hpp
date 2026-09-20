#pragma once
#include <flecs.h>
#include <vector>
namespace forge::detail {
// Engine-owned containers only; registration does not opt arbitrary opaque types
// into authoring. Admission selects an explicit ReflectedAdapter separately.
// Based on the pinned Flecs ser_std_vector example and Meta.cpp element support.
template <class T> flecs::opaque<std::vector<T>, T> reflected_vector(flecs::world& world) {
    return flecs::opaque<std::vector<T>, T>()
        .as_type(world.vector<T>())
        .serialize([](const flecs::serializer* serializer, const std::vector<T>* values) {
            for (const auto& value : *values)
                if (serializer->value(value))
                    return -1;
            return 0;
        })
        .serialize_element([](const flecs::serializer* serializer, const std::vector<T>* values,
                              std::size_t index) {
            return index < values->size() ? serializer->value((*values)[index]) : -1;
        })
        .count([](const std::vector<T>* values) { return values->size(); })
        .resize([](std::vector<T>* values, std::size_t count) { values->resize(count); })
        .ensure_element([](std::vector<T>* values, std::size_t index) {
            if (index >= values->size())
                values->resize(index + 1);
            return &(*values)[index];
        });
}
} // namespace forge::detail
