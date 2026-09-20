#pragma once
#include "../src/reflected_value.hpp"
#include <forge/identity.hpp>
#include <limits>
#include <stdexcept>
inline void test_reflected_values() {
    using namespace forge;
    using Json = nlohmann::json;
    using namespace forge::detail;
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto reject = [&](const auto& fn) {
        bool caught = false;
        try {
            fn();
        } catch (const std::exception&) {
            caught = true;
        }
        require(caught, "Unsafe reflected schema/value was accepted");
    };
    flecs::world world;
    auto schema = [&](ecs_entity_t type) { return reflected_type_schema(world, type); };
    auto valid = [&](const Json& type, const Json& value) { validate_reflected_json(type, value); };
    auto invalid = [&](const Json& type, const Json& value) {
        reject([&] { valid(type, value); });
    };
    const auto u64 = schema(flecs::U64), i64 = schema(flecs::I64);
    valid(u64, UINT64_MAX);
    valid(i64, INT64_MIN);
    valid(i64, INT64_MAX);
    invalid(i64, UINT64_MAX);
    invalid(u64, -1);
    invalid(i64, 1.0);
    invalid(i64, true);
    for (auto [id, low, high] :
         {std::tuple{flecs::I8, -128, 127}, std::tuple{flecs::I16, -32768, 32767}}) {
        auto s = schema(id);
        valid(s, low);
        valid(s, high);
        invalid(s, low - 1);
        invalid(s, high + 1);
    }
    for (auto [id, high] : {std::pair{flecs::U8, 255u}, std::pair{flecs::U16, 65535u},
                            std::pair{flecs::U32, UINT32_MAX}}) {
        auto s = schema(id);
        valid(s, high);
        invalid(s, std::uint64_t(high) + 1);
        invalid(s, -1);
    }
    // Adjacent integers above the exact-double range must remain distinguishable.
    auto ranged = u64;
    ranged["minimum"] = 0.0;
    ranged["maximum"] = 0x1p53;
    valid(ranged, std::uint64_t{1} << 53);
    invalid(ranged, (std::uint64_t{1} << 53) + 1);
    ranged["maximum"] = 0x1p64;
    valid(ranged, UINT64_MAX);
    ranged["minimum"] = 0x1p64;
    invalid(ranged, UINT64_MAX);
    ranged = i64;
    ranged["minimum"] = -0x1p63;
    ranged["maximum"] = 0x1p63;
    valid(ranged, INT64_MIN);
    valid(ranged, INT64_MAX);
    ranged["minimum"] = 0x1p63;
    invalid(ranged, INT64_MAX);
    const auto f32 = schema(flecs::F32);
    valid(f32, 0);
    valid(f32, -0.0);
    valid(f32, std::numeric_limits<float>::denorm_min());
    invalid(f32, std::numeric_limits<double>::max());
    invalid(f32, std::numeric_limits<double>::infinity());
    invalid(f32, std::numeric_limits<double>::quiet_NaN());
    valid(schema(flecs::String), "\xc3\xa9");
    invalid(schema(flecs::String), std::string("x\0y", 3));
    invalid(schema(flecs::String), std::string("\xc3", 1));
    invalid(schema(flecs::String), std::string(65536, 'a'));
    reject([&] { schema(flecs::Entity); });
    reject([&] { schema(flecs::Uptr); });
    struct Reference {
        std::uint64_t bytes[2];
    };
    auto opaque = world.component<Reference>("TestReference");
    opaque.opaque(flecs::String);
    reject([&] { schema(opaque); });
    const ReflectedReference asset[] = {{opaque.id(), "asset_ref", "mesh"}};
    auto ref = reflected_type_schema(world, opaque.id(), asset);
    const auto asset_id = AssetId::generate();
    valid(ref, asset_id);
    valid(ref, nullptr);
    invalid(ref, "not-a-uuid");
    require(ref.at("asset_type") == "mesh", "Typed reference adapter lost target type");
    const ReflectedReference entity[] = {{opaque.id(), "entity_ref"}};
    auto entity_ref = reflected_type_schema(world, opaque.id(), entity);
    valid(entity_ref, EntityRef{asset_id, EntityId::generate()});
    invalid(entity_ref, {{"entity", EntityId::generate()}});
    ecs_enum_desc_t enum_desc{};
    enum_desc.underlying_type = flecs::I64;
    enum_desc.constants[0].name = "Negative";
    enum_desc.constants[0].value = -4;
    enum_desc.constants[1].name = "Large";
    enum_desc.constants[1].value = INT64_MAX;
    auto enumeration = schema(ecs_enum_init(world.c_ptr(), &enum_desc));
    valid(enumeration, -4);
    valid(enumeration, INT64_MAX);
    invalid(enumeration, 0);
    ecs_bitmask_desc_t mask_desc{};
    mask_desc.constants[0].name = "Visible";
    mask_desc.constants[0].value = 1;
    mask_desc.constants[1].name = "Shadow";
    mask_desc.constants[1].value = 8;
    auto mask = schema(ecs_bitmask_init(world.c_ptr(), &mask_desc));
    valid(mask, 0);
    valid(mask, 9);
    invalid(mask, 2);
    invalid(mask, -1);
    ecs_array_desc_t array_desc{};
    array_desc.type = flecs::I16;
    array_desc.count = 3;
    const auto array = ecs_array_init(world.c_ptr(), &array_desc);
    auto array_schema = schema(array);
    valid(array_schema, Json::array({-1, 0, 3}));
    invalid(array_schema, Json::array({1, 2}));
    invalid(array_schema, Json::array({1, 2, 32768}));
    ecs_vector_desc_t vector_desc{};
    vector_desc.type = array;
    const auto vector = ecs_vector_init(world.c_ptr(), &vector_desc);
    auto vector_schema = schema(vector);
    valid(vector_schema, Json::array());
    valid(vector_schema, Json::array({Json::array({1, 2, 3})}));
    Json excess = Json::array();
    for (int i = 0; i != 1025; ++i)
        excess.push_back(Json::array({1, 2, 3}));
    invalid(vector_schema, excess); // Aggregate, not merely per-vector bound.
    ecs_struct_desc_t struct_desc{};
    struct_desc.members[0].name = "samples";
    struct_desc.members[0].type = vector;
    struct_desc.members[1].name = "counter";
    struct_desc.members[1].type = flecs::U64;
    const auto structure = ecs_struct_init(world.c_ptr(), &struct_desc);
    auto structured = schema(structure);
    Json value = {{"samples", Json::array({Json::array({1, 2, 3})})},
                  {"counter", UINT64_MAX},
                  {"future", {{"keep", "unknown data"}}}};
    const auto original = value;
    valid(structured, value);
    require(value == original, "Validation changed unknown fields or integer identity");
    value.erase("counter");
    invalid(structured, value);
    value = original;
    value["samples"][0][1] = 32768;
    invalid(structured, value);
    value = original;
    value["future"] = std::numeric_limits<double>::infinity();
    invalid(structured, value);
    // Padded C layouts must be checked by offsets/extents, never packed assumptions.
    struct Padded {
        std::uint8_t a;
        std::uint64_t b;
    };
    auto padded = world.component<Padded>().member<std::uint8_t>("a").member<std::uint64_t>("b");
    auto padded_schema = schema(padded);
    valid(padded_schema, {{"a", 255}, {"b", UINT64_MAX}});
    auto* metadata = ecs_get_mut(world.c_ptr(), padded.id(), EcsStruct);
    auto* members = ecs_vec_first_t(&metadata->members, ecs_member_t);
    const auto offset = members[1].offset;
    members[1].offset = 0;
    reject([&] { schema(padded); });
    members[1].offset = 1;
    reject([&] { schema(padded); });
    members[1].offset = 4096;
    reject([&] { schema(padded); });
    members[1].offset = offset;
    auto* native_type = ecs_get_mut(world.c_ptr(), padded.id(), EcsType);
    native_type->partial = true;
    reject([&] { schema(padded); });
    native_type->partial = false;
    auto deep = vector;
    for (unsigned i = 0; i != 9; ++i) {
        vector_desc.type = deep;
        deep = ecs_vector_init(world.c_ptr(), &vector_desc);
    }
    reject([&] { schema(deep); });
    // Native inline-array metadata is also preserved rather than exposed as scalar.
    struct Inline {
        float weights[3];
    };
    auto inline_type = world.component<Inline>().member<float>("weights", 3);
    auto inline_schema = schema(inline_type);
    valid(inline_schema, {{"weights", Json::array({1, 0, -1})}});
    invalid(inline_schema, {{"weights", 1}});
}
