#pragma once
#include "../src/reflected_string.hpp"
#include "../src/reflected_value.hpp"
#include "../src/reflected_vector.hpp"
#include <forge/asset_ref.hpp>
#include <limits>
#include <stdexcept>
inline void test_reflected_native() {
    using namespace forge;
    using namespace forge::detail;
    using Json = nlohmann::json;
    auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    flecs::world world;
    auto roundtrip = [&](ecs_entity_t type, const Json& value) {
        ReflectedCandidate candidate(world, type, value);
        require(read_reflected_native(world, type, candidate.data()) == value,
                "Native reflected candidate roundtrip changed values");
    };
    roundtrip(flecs::U64, UINT64_MAX);
    roundtrip(flecs::I64, INT64_MIN);
    roundtrip(flecs::F64, std::nextafter(1.0, 2.0));
    roundtrip(flecs::String, "Owned UTF-8 \xc3\xa9");
    roundtrip(flecs::Bool, true);
    ecs_enum_desc_t enumeration{};
    enumeration.underlying_type = flecs::I64;
    enumeration.constants[0].name = "Large";
    enumeration.constants[0].value = INT64_MAX;
    const auto enum_type = ecs_enum_init(world.c_ptr(), &enumeration);
    roundtrip(enum_type, INT64_MAX);
    enumeration = {};
    enumeration.underlying_type = flecs::U8;
    enumeration.constants[0].name = "Byte";
    enumeration.constants[0].value_unsigned = 255;
    roundtrip(ecs_enum_init(world.c_ptr(), &enumeration), 255);
    ecs_bitmask_desc_t mask{};
    mask.constants[0].name = "A";
    mask.constants[0].value = 1;
    mask.constants[1].name = "B";
    mask.constants[1].value = 8;
    roundtrip(ecs_bitmask_init(world.c_ptr(), &mask), 9);
    ecs_vector_desc_t vector_desc{};
    vector_desc.type = flecs::String;
    const auto strings = ecs_vector_init(world.c_ptr(), &vector_desc);
    roundtrip(strings, Json::array());
    roundtrip(strings, Json::array({"a", "b", "", "\xc3\xa9"}));
    ecs_array_desc_t array_desc{};
    array_desc.type = strings;
    array_desc.count = 2;
    const auto arrays = ecs_array_init(world.c_ptr(), &array_desc);
    roundtrip(arrays, Json::array({Json::array({"left", "right"}), Json::array()}));
    ecs_struct_desc_t first{};
    first.members[0].name = "label";
    first.members[0].type = flecs::String;
    first.members[1].name = "count";
    first.members[1].type = flecs::U64;
    first.members[2].name = "groups";
    first.members[2].type = arrays;
    const auto type_a = ecs_struct_init(world.c_ptr(), &first);
    std::swap(first.members[0], first.members[1]);
    const auto type_b = ecs_struct_init(world.c_ptr(), &first);
    Json value = {{"label", "Source"},
                  {"count", UINT64_MAX},
                  {"groups", Json::array({Json::array({"one", "two"}), Json::array({"three"})})}};
    ReflectedCandidate source(world, type_a, value);
    const auto copied = read_reflected_native(world, type_a, source.data());
    ReflectedCandidate destination(world, type_b, copied);
    require(read_reflected_native(world, type_b, destination.data()) == value,
            "Value transport depended on native C layout/member order");
    const auto& members_a = *ecs_get(world.c_ptr(), type_a, EcsStruct);
    const auto& members_b = *ecs_get(world.c_ptr(), type_b, EcsStruct);
    require(std::string(ecs_vec_first_t(&members_a.members, ecs_member_t)[0].name) !=
                std::string(ecs_vec_first_t(&members_b.members, ecs_member_t)[0].name),
            "Cross-layout fixture did not change physical member order");
    // Reader never pushes a mutable vector cursor, so no truncation/reallocation.
    ReflectedCandidate string_value(world, strings, Json::array({"one", "two"}));
    const auto before = *static_cast<const ecs_vec_t*>(string_value.data());
    (void)read_reflected_native(world, strings, string_value.data());
    const auto after = *static_cast<const ecs_vec_t*>(string_value.data());
    require(before.array == after.array && before.count == after.count && before.size == after.size,
            "Reading a native vector mutated its storage");
    struct Inline {
        std::uint64_t numbers[3];
    };
    auto inlined = world.component<Inline>().member<std::uint64_t>("numbers", 3);
    roundtrip(inlined, {{"numbers", Json::array({0, UINT64_MAX, std::uint64_t{1} << 63})}});
    using Ref = AssetRef<SceneAsset>;
    auto reference = world.component<Ref>("TestSceneRef");
    reference.opaque(flecs::String);
    const ReflectedAdapter adapter[] = {{reference.id(), "asset_ref", "scene",
                                         [](const void* ptr) -> Json {
                                             const auto& v = *static_cast<const Ref*>(ptr);
                                             return v.id ? Json(v.id) : Json();
                                         },
                                         [](void* ptr, const Json& v) {
                                             static_cast<Ref*>(ptr)->id =
                                                 v.is_null() ? AssetId{} : v.get<AssetId>();
                                         }}};
    const auto id = AssetId::generate();
    ReflectedCandidate asset(world, reference, id, adapter);
    require(read_reflected_native(world, reference, asset.data(), adapter) == Json(id),
            "Native typed reference transport lost identity");
    ReflectedCandidate empty(world, reference, nullptr, adapter);
    require(read_reflected_native(world, reference, empty.data(), adapter).is_null(),
            "Null reference transport failed");
    auto ref_vector = world.component<std::vector<Ref>>("TestSceneRefVector");
    ref_vector.opaque(reflected_vector<Ref>);
    const ReflectedAdapter vector_adapters[] = {adapter[0], {ref_vector.id(), "vector"}};
    const Json slot_values = Json::array({id, nullptr, AssetId::generate()});
    ReflectedCandidate slots(world, ref_vector, slot_values, vector_adapters);
    const auto& native_slots = *static_cast<const std::vector<Ref>*>(slots.data());
    const auto* slot_pointer = native_slots.data();
    const auto slot_capacity = native_slots.capacity();
    require(read_reflected_native(world, ref_vector, slots.data(), vector_adapters) ==
                    slot_values &&
                native_slots.data() == slot_pointer && native_slots.capacity() == slot_capacity,
            "Engine-owned opaque vector adapter lost refs or mutated storage");
    ReflectedCandidate no_slots(world, ref_vector, Json::array(), vector_adapters);
    require(read_reflected_native(world, ref_vector, no_slots.data(), vector_adapters).empty(),
            "Empty engine vector roundtrip failed");
    bool refused = false;
    try {
        (void)reflected_type_schema(world, ref_vector, adapter);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, "Opaque vector was admitted without its explicit engine adapter");
    std::vector<Ref> excessive(4097);
    refused = false;
    try {
        (void)read_reflected_native(world, ref_vector, &excessive, vector_adapters);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, "Opaque vector count was not bounded before reading elements");
    // Deliberately incomplete/wrong native callbacks are rejected, even when a
    // consumer accidentally puts such a type on its engine adapter allowlist.
    auto* native_adapter = ecs_get_mut(world.c_ptr(), ref_vector.id(), EcsOpaque);
    const auto good_element = native_adapter->serialize_element;
    native_adapter->serialize_element = nullptr;
    refused = false;
    try {
        (void)reflected_type_schema(world, ref_vector, vector_adapters);
    } catch (const std::exception&) {
        refused = true;
    }
    native_adapter->serialize_element = good_element;
    require(refused, "Incomplete opaque vector adapter admitted");
    native_adapter->serialize_element = [](const ecs_serializer_t* s, const void*, std::size_t) {
        const std::uint64_t wrong = 0;
        return s->value(flecs::U64, &wrong);
    };
    refused = false;
    try {
        (void)read_reflected_native(world, ref_vector, slots.data(), vector_adapters);
    } catch (const std::exception&) {
        refused = true;
    }
    native_adapter->serialize_element = good_element;
    require(refused, "Opaque vector callback changed the admitted element type");
    auto owned_string = world.component<std::string>("EngineString");
    owned_string.opaque(reflected_string);
    auto string_vector = world.component<std::vector<std::string>>("EngineStringVector");
    string_vector.opaque(reflected_vector<std::string>);
    const ReflectedAdapter string_adapters[] = {{owned_string.id(), "string"},
                                                {string_vector.id(), "vector"}};
    const auto text_values = Json::array({"slot:paint", "UTF-8 \xc3\xa9", ""});
    ReflectedCandidate text_candidate(world, string_vector, text_values, string_adapters);
    require(read_reflected_native(world, string_vector, text_candidate.data(), string_adapters) ==
                text_values,
            "Explicit engine string/vector adapter failed");
    std::string oversized(65536, 'a');
    refused = false;
    try {
        (void)read_reflected_native(world, owned_string, &oversized, string_adapters);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, "Native opaque string allocation was not bounded");
    std::string embedded_zero("a\0b", 3);
    refused = false;
    try {
        (void)read_reflected_native(world, owned_string, &embedded_zero, string_adapters);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, "Native string silently truncated an embedded zero");
    refused = false;
    try {
        (void)reflected_type_schema(world, owned_string);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, "String opaque was admitted without explicit adapter");
    auto* string_opaque = ecs_get_mut(world.c_ptr(), owned_string.id(), EcsOpaque);
    const auto good_string = string_opaque->serialize;
    string_opaque->serialize = [](const ecs_serializer_t*, const void*) { return 0; };
    refused = false;
    try {
        (void)read_reflected_native(world, owned_string, &oversized, string_adapters);
    } catch (const std::exception&) {
        refused = true;
    }
    string_opaque->serialize = good_string;
    require(refused, "String opaque silently omitted the value");
    auto moved = std::move(source);
    require(!source.data() && read_reflected_native(world, type_a, moved.data()) == value,
            "Detached native value move lost ownership");
    moved = std::move(destination);
    require(!destination.data() && read_reflected_native(world, type_b, moved.data()) == value,
            "Detached native value move assignment lost ownership");
    // A late adapter failure must release earlier native string/vector allocations.
    auto failing_adapter = adapter[0];
    failing_adapter.assign = [](void*, const Json&) {
        throw std::runtime_error("Test adapter failure");
    };
    ecs_struct_desc_t failing{};
    failing.members[0].name = "owned";
    failing.members[0].type = strings;
    failing.members[1].name = "reference";
    failing.members[1].type = reference;
    const auto failing_type = ecs_struct_init(world.c_ptr(), &failing);
    bool rejected = false;
    try {
        ReflectedCandidate invalid(
            world, failing_type,
            {{"owned", Json::array({"allocated before failure"})}, {"reference", id}},
            {&failing_adapter, 1});
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "Failing adapter unexpectedly published a candidate");
    require(read_reflected_native(world, type_b, moved.data()) == value,
            "Failed candidate changed the previous usable value");
}
