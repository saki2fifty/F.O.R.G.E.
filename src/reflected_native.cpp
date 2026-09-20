#include "reflected_value.hpp"
#include <algorithm>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
void checked(int result) {
    if (result)
        throw std::runtime_error("Native Meta value assignment failed");
}
const ReflectedAdapter& adapter_for(std::span<const ReflectedAdapter> refs, ecs_entity_t type) {
    const auto it = std::ranges::find(refs, type, &ReflectedAdapter::type);
    if (it == refs.end())
        throw std::runtime_error("Missing explicit native reference adapter");
    return *it;
}
void write(ecs_meta_cursor_t& cursor, const Json& schema, const Json& value,
           std::span<const ReflectedAdapter> references) {
    const auto kind = schema.at("type").get<std::string>();
    if (kind == "struct") {
        checked(ecs_meta_push(&cursor));
        for (const auto& field : schema.at("fields")) {
            const auto& name = field.at("id").get_ref<const std::string&>();
            checked(ecs_meta_member(&cursor, name.c_str()));
            write(cursor, field, value.at(name), references);
        }
        checked(ecs_meta_pop(&cursor));
    } else if (kind == "array" || kind == "vector") {
        checked(ecs_meta_push(&cursor));
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i)
                checked(ecs_meta_next(&cursor));
            write(cursor, schema.at("element"), value[i], references);
        }
        checked(ecs_meta_pop(&cursor));
    } else if (kind == "asset_ref" || kind == "entity_ref") {
        const auto& ref = adapter_for(references, ecs_meta_get_type(&cursor));
        if (!ref.assign)
            throw std::runtime_error("Reference adapter has no native assignment");
        ref.assign(ecs_meta_get_ptr(&cursor), value);
    } else if (kind == "bool")
        checked(ecs_meta_set_bool(&cursor, value.get<bool>()));
    else if (kind == "string")
        checked(ecs_meta_set_string(&cursor, value.get_ref<const std::string&>().c_str()));
    else if (kind.starts_with("uint"))
        checked(ecs_meta_set_uint(&cursor, value.get<std::uint64_t>()));
    else if (kind.starts_with("int"))
        checked(ecs_meta_set_int(&cursor, value.get<std::int64_t>()));
    else if (kind == "float32" || kind == "float64")
        checked(ecs_meta_set_float(&cursor, value.get<double>()));
    else
        throw std::runtime_error("Unsupported native reflected assignment");
}
struct Reader {
    ecs_world_t* world;
    std::span<const ReflectedAdapter> references;
    std::size_t elements = 0, bytes = 0;
    void consume(std::size_t count) {
        if (count > 4096 - elements)
            throw std::runtime_error("Native reflected container budget exceeded");
        elements += count;
    }
    std::string string(const char* value) {
        if (!value)
            return {};
        std::size_t size = 0;
        while (size < 65536 - bytes && value[size])
            ++size;
        if (size == 65536 - bytes)
            throw std::runtime_error("Native reflected string budget exceeded");
        bytes += size;
        return {value, size};
    }
    Json sequence(ecs_entity_t type, const Json& schema, const void* data, std::size_t count) {
        consume(count);
        const auto* layout = ecs_get(world, type, EcsComponent);
        if (!layout || layout->size <= 0 || (count && !data))
            throw std::runtime_error("Invalid native collection storage");
        auto result = Json::array();
        for (std::size_t i = 0; i < count; ++i)
            result.push_back(read(
                type, schema, static_cast<const std::byte*>(data) + i * std::size_t(layout->size)));
        return result;
    }
    Json read(ecs_entity_t type, const Json& schema, const void* value) {
        if (!value)
            throw std::runtime_error("Missing native reflected value");
        const auto kind = schema.at("type").get<std::string>();
        if (kind == "struct") {
            const auto* structure = ecs_get(world, type, EcsStruct);
            const auto* members = ecs_vec_first_t(&structure->members, ecs_member_t);
            consume(schema.at("fields").size());
            Json result = Json::object();
            for (const auto& field : schema.at("fields")) {
                const auto& key = field.at("id").get_ref<const std::string&>();
                const ecs_member_t* member = nullptr;
                for (int i = 0; i < ecs_vec_count(&structure->members); ++i)
                    if (members[i].name == key) {
                        member = &members[i];
                        break;
                    }
                if (!member)
                    throw std::runtime_error("Native member metadata changed while reading");
                const auto* ptr = static_cast<const std::byte*>(value) + member->offset;
                if (member->count > 1)
                    result[key] = sequence(member->type, field.at("element"), ptr,
                                           std::size_t(member->count));
                else
                    result[key] = read(member->type, field, ptr);
            }
            return result;
        }
        if (kind == "array") {
            const auto& array = *ecs_get(world, type, EcsArray);
            return sequence(array.type, schema.at("element"), value, std::size_t(array.count));
        }
        if (kind == "vector") {
            if (const auto* opaque = ecs_get(world, type, EcsOpaque)) {
                (void)adapter_for(references,
                                  type); // Projection admitted this exact engine adapter.
                const auto& vector = *ecs_get(world, opaque->as_type, EcsVector);
                const auto count = opaque->count(value);
                consume(count);
                Json result = Json::array();
                for (std::size_t i = 0; i < count; ++i) {
                    struct Element {
                        Reader* reader;
                        ecs_entity_t type;
                        const Json* schema;
                        Json value;
                        std::exception_ptr error;
                        bool called = false;
                    } element{this, vector.type, &schema.at("element"), {}, {}, false};
                    ecs_serializer_t serializer{};
                    serializer.world = world;
                    serializer.ctx = &element;
                    serializer.value_ = [](const ecs_serializer_t* ser, ecs_entity_t element_type,
                                           const void* ptr) -> int {
                        auto& e = *static_cast<Element*>(ser->ctx);
                        try {
                            if (e.called || element_type != e.type)
                                throw std::runtime_error(
                                    "Native vector adapter emitted wrong/duplicate element");
                            e.called = true;
                            e.value = e.reader->read(element_type, *e.schema, ptr);
                            return 0;
                        } catch (...) {
                            e.error = std::current_exception();
                            return -1;
                        }
                    };
                    serializer.member_ = [](const ecs_serializer_t* ser, const char*) -> int {
                        auto& e = *static_cast<Element*>(ser->ctx);
                        e.error = std::make_exception_ptr(std::runtime_error(
                            "Native vector adapter emitted an unexpected named member"));
                        return -1;
                    };
                    const auto status = opaque->serialize_element(&serializer, value, i);
                    if (element.error)
                        std::rethrow_exception(element.error);
                    checked(status);
                    if (!element.called)
                        throw std::runtime_error("Native vector adapter omitted an element");
                    result.push_back(std::move(element.value));
                }
                return result;
            }
            const auto& vector = *ecs_get(world, type, EcsVector);
            const auto& storage = *static_cast<const ecs_vec_t*>(value);
            if (storage.count < 0 || storage.size < storage.count)
                throw std::runtime_error("Invalid native vector count/capacity");
            return sequence(vector.type, schema.at("element"), ecs_vec_first(&storage),
                            std::size_t(ecs_vec_count(&storage)));
        }
        if (kind == "asset_ref" || kind == "entity_ref") {
            const auto& ref = adapter_for(references, type);
            if (!ref.read)
                throw std::runtime_error("Reference adapter has no native reader");
            return ref.read(value);
        }
        // Pinned enum cursor getters read i32, independently of underlying_kind.
        // Use the enum's native primitive metadata at the same local pointer.
        if (const auto* enumeration = ecs_get(world, type, EcsEnum))
            type = enumeration->underlying_type;
        auto cursor = ecs_meta_cursor(world, type, const_cast<void*>(value));
        if (!cursor.valid)
            throw std::runtime_error("Missing native value cursor");
        if (kind == "bool")
            return ecs_meta_get_bool(&cursor);
        if (kind == "string")
            return string(ecs_meta_get_string(&cursor));
        if (kind.starts_with("uint"))
            return ecs_meta_get_uint(&cursor);
        if (kind.starts_with("int"))
            return ecs_meta_get_int(&cursor);
        if (kind == "float32" || kind == "float64")
            return ecs_meta_get_float(&cursor);
        throw std::runtime_error("Unsupported native reflected read");
    }
};
} // namespace
ReflectedCandidate::ReflectedCandidate(flecs::world world, ecs_entity_t type, const Json& value,
                                       std::span<const ReflectedAdapter> references)
    : world_(world.c_ptr()), type_(type) {
    const auto schema = reflected_type_schema(world, type, references);
    validate_reflected_json(schema, value);
    value_ = ecs_value_new(world_, type_);
    if (!value_)
        throw std::runtime_error("Could not allocate detached reflected value");
    try {
        auto cursor = ecs_meta_cursor(world_, type_, value_);
        if (!cursor.valid)
            throw std::runtime_error("Missing native value cursor");
        write(cursor, schema, value, references);
        // Test actual stored/converted values too. This also catches unsupported
        // engine adapters before a caller can publish the candidate.
        validate_reflected_json(schema, read_reflected_native(world, type, value_, references));
    } catch (...) {
        ecs_value_free(world_, type_, value_);
        value_ = nullptr;
        throw;
    }
}
ReflectedCandidate::~ReflectedCandidate() {
    if (value_)
        ecs_value_free(world_, type_, value_);
}
ReflectedCandidate::ReflectedCandidate(ReflectedCandidate&& other) noexcept
    : world_(other.world_), type_(other.type_), value_(std::exchange(other.value_, nullptr)) {}
ReflectedCandidate& ReflectedCandidate::operator=(ReflectedCandidate&& other) noexcept {
    if (this != &other) {
        if (value_)
            ecs_value_free(world_, type_, value_);
        world_ = other.world_;
        type_ = other.type_;
        value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
}
Json read_reflected_native(flecs::world world, ecs_entity_t type, const void* value,
                           std::span<const ReflectedAdapter> references) {
    const auto schema = reflected_type_schema(world, type, references);
    Reader reader{world.c_ptr(), references};
    auto result = reader.read(type, schema, value);
    validate_reflected_json(schema, result);
    return result;
}
} // namespace forge::detail
