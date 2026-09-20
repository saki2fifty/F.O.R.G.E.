#pragma once
#include "../src/reflected_extensions.hpp"
#include "../src/reflected_string.hpp"
#include "../src/reflected_value.hpp"
#include "../src/reflected_vector.hpp"
#include <algorithm>
#include <cstddef>
inline void test_reflected_extensions() {
    using namespace forge::detail;
    using Json = nlohmann::json;
    auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto rejects = [&](auto fn) {
        bool caught = false;
        try {
            fn();
        } catch (const std::exception&) {
            caught = true;
        }
        require(caught, "Invalid keyed reflected value accepted");
    };
    flecs::world world;
    world.component<ReflectedSequenceKey>(reflected_sequence_key_type);
    const auto string = world.component<std::string>("EngineString");
    world.component<std::string>().opaque(reflected_string);
    struct Entry {
        std::string key;
        double value;
    };
    auto entry = world.component<Entry>("Entry").member<std::string>("key").member<double>("value");
    auto entries = world.component<std::vector<Entry>>("Entries");
    entries.opaque(reflected_vector<Entry>);
    struct Container {
        std::vector<Entry> items;
    };
    auto container = world.component<Container>("Container");
    ecs_struct_desc_t description{};
    description.entity = container.id();
    description.create_member_entities = true;
    description.members[0].name = "items";
    description.members[0].type = entries.id();
    description.members[0].offset = offsetof(Container, items);
    description.members[0].use_offset = true;
    require(ecs_struct_init(world.c_ptr(), &description) == container.id(),
            "Container metadata registration failed");
    container.lookup("items").set<ReflectedSequenceKey>({"key"});
    const ReflectedAdapter adapters[] = {{string.id(), "string"}, {entries.id(), "vector"}};
    const auto schema = reflected_type_schema(world, container, adapters);
    require(schema.at("fields")[0].at("element_key") == "key", "Native entry key metadata lost");
    Json source{
        {"future_root", {"unknown", nullptr}},
        {"items",
         Json::array({{{"key", "paint"}, {"value", 1.0}, {"future", {{"texture", "opaque"}}}},
                      {{"key", "metal"}, {"value", 2.0}, {"future", nullptr}}})}};
    validate_reflected_json(schema, source);
    const auto extensions = reflected_extensions(schema, source);
    require(!extensions.at("items").at("paint").contains("value") &&
                !extensions.at("items").at("paint").contains("key"),
            "Opaque envelope retained a second copy of known authored fields");
    ReflectedCandidate candidate(world, container, source, adapters);
    auto known = read_reflected_native(world, container, candidate.data(), adapters);
    require(merge_reflected_extensions(schema, known, extensions) == source,
            "Nested native/unknown roundtrip lost data");
    std::reverse(known["items"].begin(), known["items"].end());
    known["items"][1]["value"] = 99.0;
    const auto reordered = merge_reflected_extensions(schema, known, extensions);
    require(reordered.at("items")[0].at("future").is_null() &&
                reordered.at("items")[1].at("future").at("texture") == "opaque" &&
                reordered.at("items")[1].at("value") == 99.0,
            "Unknown entry extension followed array position instead of stable key");
    known["items"].erase(known["items"].begin());
    known["items"].push_back({{"key", "new"}, {"value", 4.0}});
    auto removed = merge_reflected_extensions(schema, known, extensions);
    require(removed.at("items").size() == 2 && !removed.at("items")[1].contains("future"),
            "Removed entry extension was resurrected on a new entry");
    auto bad = source;
    bad["items"][1]["key"] = "paint";
    rejects([&] { validate_reflected_json(schema, bad); });
    bad["items"][1]["key"] = "";
    rejects([&] { validate_reflected_json(schema, bad); });
    container.lookup("items").set<ReflectedSequenceKey>({"value"});
    rejects([&] { reflected_type_schema(world, container, adapters); });
    container.lookup("items").remove<ReflectedSequenceKey>();
    const auto positional = reflected_type_schema(world, container, adapters);
    validate_reflected_json(positional, source);
    require(merge_reflected_extensions(
                positional, read_reflected_native(world, container, candidate.data(), adapters),
                reflected_extensions(positional, source)) == source,
            "Positional native vector extensions lost on unchanged roundtrip");
    auto clean = source;
    clean.erase("future_root");
    for (auto& e : clean["items"])
        e.erase("future");
    require(reflected_extensions(positional, clean).is_null(),
            "Known-only values created opaque shadow state");
}
