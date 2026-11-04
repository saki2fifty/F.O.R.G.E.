#include "native_system_access.hpp"
#include "json_output_text.hpp"
#include "utf8_boundary.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>
namespace forge::detail {
namespace {
std::string label(std::string_view text) {
    std::string value(text.substr(0, utf8_range_end(text, 0, 160)));
    if (text.size() > 160) {
        value.resize(utf8_range_end(value, 0, 157));
        value += "...";
    }
    return json_output_text(value).get<std::string>();
}
const char* access_name(int value) {
    switch (value) {
    case EcsInOutDefault:
        return "default";
    case EcsInOutNone:
        return "none";
    case EcsInOutFilter:
        return "filter";
    case EcsIn:
        return "read";
    case EcsOut:
        return "write";
    case EcsInOut:
        return "read_write";
    default:
        return "unknown";
    }
}
const char* operator_name(int value) {
    switch (value) {
    case EcsAnd:
        return "and";
    case EcsOr:
        return "or";
    case EcsNot:
        return "not";
    case EcsOptional:
        return "optional";
    case EcsAndFrom:
        return "and_from";
    case EcsOrFrom:
        return "or_from";
    case EcsNotFrom:
        return "not_from";
    default:
        return "unknown";
    }
}
bool text(const Json& row, const char* key) {
    return row.contains(key) && row.at(key).is_string() &&
           row.at(key).get_ref<const std::string&>().size() <= 480;
}
bool integer(const Json& value, std::size_t maximum) {
    return value.is_number_integer() && value >= 0 && value <= maximum;
}
bool one_of(const Json& value, std::initializer_list<const char*> choices) {
    return std::any_of(choices.begin(), choices.end(),
                       [&](const char* choice) { return value == choice; });
}
} // namespace
std::string native_system_display_name(flecs::world& world, ecs_entity_t id) {
    if (!id)
        return "";
    const auto path = world.entity(id).path();
    return path.c_str() ? label(path.c_str()) : "";
}
Json native_system_access(flecs::world& world, const ecs_query_t* query, std::size_t& remaining) {
    const auto count = query ? std::size_t(query->term_count) : 0;
    const auto copied = std::min({count, remaining, native_access_row_capacity});
    Json terms = Json::array();
    bool simple = true;
    for (std::size_t i = 0; i < count; ++i)
        simple &= (query->terms[i].oper == EcsAnd || query->terms[i].oper == EcsOptional) &&
                  query->terms[i].id != EcsScopeOpen && query->terms[i].id != EcsScopeClose;
    for (std::size_t i = 0; i < copied; ++i) {
        const auto& term = query->terms[i];
        const auto field = term.field_index;
        const auto bit = field >= 0 && field < 64 ? std::uint64_t{1} << field : 0;
        const bool data = query->data_fields & bit;
        const auto source_id = term.src.id & ~EcsTermRefFlags;
        const bool traverses = term.src.id & (EcsUp | EcsTrav | EcsCascade | EcsDesc);
        const char* source = "unresolved";
        if (ecs_term_match_0(&term))
            source = "none";
        else if (ecs_term_match_this(&term))
            source = !traverses && (term.src.id & EcsSelf) ? "self" : "traversal";
        else if (term.src.id & EcsIsEntity)
            source = traverses ? "traversal" : "entity";
        const bool fixed = std::string_view(source) == "entity";
        const bool fixed_origin = source_id && (term.src.id & EcsIsEntity);
        // Decode the documented declaration. The selected native simple-query
        // path does not populate every read/write summary mask consistently.
        const bool data_access = data && (term.inout == EcsInOutDefault || term.inout == EcsIn ||
                                          term.inout == EcsOut || term.inout == EcsInOut);
        const bool reads = data_access && term.inout != EcsOut;
        const bool writes =
            data_access && (term.inout == EcsOut || term.inout == EcsInOut ||
                            (term.inout == EcsInOutDefault && ecs_term_match_this(&term)));
        const bool comparable = simple && data_access && !ecs_id_is_wildcard(term.id) &&
                                (fixed || std::string_view(source) == "self");
        // A no-data output annotation may declare structural work. Do not call
        // it harmless just because the iterator does not expose a data field.
        const bool ignored = term.inout == EcsInOutNone || term.inout == EcsInOutFilter ||
                             (!data && term.inout == EcsInOutDefault && !ecs_term_match_0(&term));
        std::unique_ptr<char, decltype(ecs_os_api.free_)> name(ecs_id_str(world, term.id),
                                                               ecs_os_api.free_);
        terms.push_back(
            {{"index", i},
             {"id", term.id},
             {"name", name ? label(name.get()) : ""},
             {"access", access_name(term.inout)},
             {"operator", operator_name(term.oper)},
             {"source", source},
             {"source_id", fixed_origin ? source_id : 0},
             {"source_name", fixed_origin ? native_system_display_name(world, source_id) : ""},
             {"data", data},
             {"reads", reads},
             {"writes", writes},
             {"analysis", comparable ? "included"
                          : ignored  ? "ignored"
                                     : "unresolved"}});
    }
    remaining -= copied;
    return {{"total_terms", count},
            {"truncated", count > copied},
            {"matches_this", query && bool(query->flags & EcsQueryMatchThis)},
            {"terms", std::move(terms)}};
}
Json native_system_access_overlaps(const Json& entries) {
    struct Reference {
        std::size_t entry;
        const Json* term;
    };
    std::map<std::uint64_t, std::vector<Reference>> groups;
    std::size_t copied = 0, total = 0, unresolved = 0, comparisons = 0;
    bool terms_truncated = false, truncated = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& row = entries[i];
        if (row.at("kind") != "system")
            continue;
        const auto& access = row.at("access");
        total += access.at("total_terms").get<std::size_t>();
        terms_truncated |= access.at("truncated").get<bool>();
        for (const auto& term : access.at("terms")) {
            ++copied;
            unresolved += term.at("analysis") == "unresolved";
            if (term.at("analysis") == "included")
                groups[term.at("id").get<std::uint64_t>()].push_back({i, &term});
        }
    }
    Json pairs = Json::array();
    std::set<std::tuple<std::size_t, std::size_t, std::uint64_t, std::uint64_t, std::uint64_t>>
        seen;
    for (const auto& [id, references] : groups) {
        for (std::size_t a = 0; a < references.size() && !truncated; ++a)
            for (std::size_t b = a + 1; b < references.size(); ++b) {
                if (comparisons == native_access_comparison_capacity ||
                    pairs.size() == native_access_overlap_capacity) {
                    truncated = true;
                    break;
                }
                ++comparisons;
                const auto& first = references[a];
                const auto& second = references[b];
                const auto& left = *first.term;
                const auto& right = *second.term;
                if (first.entry == second.entry ||
                    (!left.at("writes").get<bool>() && !right.at("writes").get<bool>()))
                    continue;
                const bool same_entity =
                    left.at("source") == "entity" && right.at("source") == "entity";
                if (same_entity && left.at("source_id") != right.at("source_id"))
                    continue;
                if (!seen.emplace(first.entry, second.entry, id,
                                  left.at("source_id").get<std::uint64_t>(),
                                  right.at("source_id").get<std::uint64_t>())
                         .second)
                    continue;
                pairs.push_back(
                    {{"first", first.entry},
                     {"second", second.entry},
                     {"id", id},
                     {"component", left.at("name")},
                     {"first_term", left.at("index")},
                     {"second_term", right.at("index")},
                     {"kind", left.at("writes").get<bool>() && right.at("writes").get<bool>()
                                  ? "write_write"
                                  : "read_write"},
                     {"scope", same_entity ? "same_entity" : "possible_entities"}});
            }
        if (truncated)
            break;
    }
    return {{"term_capacity", native_access_term_capacity},
            {"copied_terms", copied},
            {"total_terms", total},
            {"terms_truncated", terms_truncated},
            {"unresolved_terms", unresolved},
            {"capacity", native_access_overlap_capacity},
            {"comparison_capacity", native_access_comparison_capacity},
            {"comparisons", comparisons},
            {"truncated", truncated},
            {"pairs", std::move(pairs)}};
}
namespace {
struct WorkerAccessCoverage {
    std::size_t systems = 0, copied = 0, total = 0, unresolved = 0, total_writes = 0;
    bool terms_truncated = false;
    std::vector<std::pair<std::size_t, std::size_t>> writes;
};
WorkerAccessCoverage worker_access_coverage(const Json& entries, unsigned workers) {
    if (!entries.is_array() || entries.size() > native_system_plan_capacity || !workers ||
        workers > 16)
        throw std::runtime_error("Invalid native worker access scope");
    WorkerAccessCoverage result;
    if (workers == 1)
        return result;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& row = entries[i];
        if (row.at("kind") != "system" || !row.at("multi_threaded").get<bool>())
            continue;
        const auto& access = row.at("access");
        if (!access.at("matches_this").get<bool>())
            continue; // Native no-This iteration is not partitioned over entity rows.
        ++result.systems;
        result.total += access.at("total_terms").get<std::size_t>();
        result.terms_truncated |= access.at("truncated").get<bool>();
        for (const auto& term : access.at("terms")) {
            ++result.copied;
            result.unresolved += term.at("analysis") == "unresolved";
            if (term.at("analysis") != "included" || term.at("source") != "entity" ||
                !term.at("writes").get<bool>())
                continue;
            ++result.total_writes;
            if (result.writes.size() < native_worker_write_capacity)
                result.writes.emplace_back(i, term.at("index").get<std::size_t>());
        }
    }
    return result;
}
} // namespace
Json native_system_worker_access(const Json& entries, unsigned workers) {
    const auto coverage = worker_access_coverage(entries, workers);
    Json writes = Json::array();
    for (const auto& [system, term] : coverage.writes)
        writes.push_back({{"system", system}, {"term", term}});
    return {{"workers", workers},
            {"system_count", coverage.systems},
            {"copied_terms", coverage.copied},
            {"total_terms", coverage.total},
            {"unresolved_terms", coverage.unresolved},
            {"terms_truncated", coverage.terms_truncated},
            {"capacity", native_worker_write_capacity},
            {"total_writes", coverage.total_writes},
            {"truncated", coverage.total_writes > coverage.writes.size()},
            {"writes", std::move(writes)}};
}
void validate_native_system_worker_access(const Json& entries, const Json& observation,
                                          unsigned workers) {
    const auto coverage = worker_access_coverage(entries, workers);
    const std::pair<const char*, std::size_t> counters[] = {
        {"workers", workers},
        {"system_count", coverage.systems},
        {"copied_terms", coverage.copied},
        {"total_terms", coverage.total},
        {"unresolved_terms", coverage.unresolved},
        {"capacity", native_worker_write_capacity},
        {"total_writes", coverage.total_writes}};
    for (const auto& [key, expected] : counters)
        if (!integer(observation.at(key), native_system_plan_capacity * 127) ||
            observation.at(key) != expected)
            throw std::runtime_error("Invalid native worker access counter");
    const auto& writes = observation.at("writes");
    if (!writes.is_array() || writes.size() != coverage.writes.size() ||
        !observation.at("terms_truncated").is_boolean() ||
        observation.at("terms_truncated") != coverage.terms_truncated ||
        !observation.at("truncated").is_boolean() ||
        observation.at("truncated") != (coverage.total_writes > coverage.writes.size()))
        throw std::runtime_error("Invalid native worker access coverage");
    for (std::size_t i = 0; i < writes.size(); ++i)
        if (!integer(writes[i].at("system"), native_system_plan_capacity - 1) ||
            !integer(writes[i].at("term"), native_access_row_capacity - 1) ||
            writes[i].at("system") != coverage.writes[i].first ||
            writes[i].at("term") != coverage.writes[i].second)
            throw std::runtime_error("Native worker write lost its declaration");
}
void validate_native_system_access(const Json& entries, const Json* overlaps) {
    if (!entries.is_array() || entries.size() > native_system_plan_capacity)
        throw std::runtime_error("Invalid native access plan");
    std::size_t count = 0, total = 0, unresolved = 0;
    bool terms_truncated = false;
    for (const auto& row : entries) {
        if (!row.contains("access"))
            continue; // Older copied snapshots remain readable.
        const auto& access = row.at("access");
        const auto& terms = access.at("terms");
        if (access.contains("matches_this") && !access.at("matches_this").is_boolean())
            throw std::runtime_error("Invalid native This-source observation");
        if (row.at("kind") != "system" || !terms.is_array() ||
            terms.size() > native_access_row_capacity || !integer(access.at("total_terms"), 127) ||
            access.at("total_terms") < terms.size() || !access.at("truncated").is_boolean() ||
            access.at("truncated") != (access.at("total_terms") > terms.size()))
            throw std::runtime_error("Invalid native access terms");
        total += access.at("total_terms").get<std::size_t>();
        terms_truncated |= access.at("truncated").get<bool>();
        for (std::size_t i = 0; i < terms.size(); ++i) {
            const auto& term = terms[i];
            if (!integer(term.at("index"), native_access_row_capacity - 1) ||
                term.at("index") != i || !term.at("id").is_number_unsigned() ||
                !term.at("source_id").is_number_unsigned() || !text(term, "name") ||
                !text(term, "source_name") ||
                !one_of(term.at("access"),
                        {"default", "none", "filter", "read", "write", "read_write", "unknown"}) ||
                !one_of(term.at("operator"), {"and", "or", "not", "optional", "and_from", "or_from",
                                              "not_from", "unknown"}) ||
                !one_of(term.at("source"), {"self", "entity", "none", "traversal", "unresolved"}) ||
                !one_of(term.at("analysis"), {"included", "ignored", "unresolved"}) ||
                !term.at("data").is_boolean() || !term.at("reads").is_boolean() ||
                !term.at("writes").is_boolean() ||
                (!one_of(term.at("source"), {"entity", "traversal"}) &&
                 term.at("source_id") != 0) ||
                (term.at("source") == "entity" && term.at("source_id") == 0) ||
                (term.at("analysis") == "included" &&
                 (!term.at("data").get<bool>() || term.at("id") == 0 ||
                  !one_of(term.at("operator"), {"and", "optional"}) ||
                  one_of(term.at("access"), {"none", "filter", "unknown"}) ||
                  !one_of(term.at("source"), {"self", "entity"}))))
                throw std::runtime_error("Invalid native access declaration");
            ++count;
            unresolved += term.at("analysis") == "unresolved";
        }
    }
    if (count > native_access_term_capacity)
        throw std::runtime_error("Native access capacity exceeded");
    if (!overlaps)
        return;
    const auto& value = *overlaps;
    const auto& pairs = value.at("pairs");
    for (const char* key : {"term_capacity", "copied_terms", "total_terms", "unresolved_terms",
                            "capacity", "comparison_capacity", "comparisons"})
        if (!integer(value.at(key), native_system_plan_capacity * 127))
            throw std::runtime_error("Invalid native access diagnostic counter");
    if (!pairs.is_array() || pairs.size() > native_access_overlap_capacity ||
        value.at("term_capacity") != native_access_term_capacity ||
        value.at("capacity") != native_access_overlap_capacity ||
        value.at("comparison_capacity") != native_access_comparison_capacity ||
        value.at("copied_terms") != count || value.at("total_terms") != total ||
        value.at("unresolved_terms") != unresolved ||
        value.at("terms_truncated") != terms_truncated ||
        !value.at("terms_truncated").is_boolean() || !value.at("truncated").is_boolean() ||
        !integer(value.at("comparisons"), native_access_comparison_capacity))
        throw std::runtime_error("Invalid native access diagnostic bounds");
    for (const auto& pair : pairs) {
        if (!integer(pair.at("first"), entries.size()) ||
            !integer(pair.at("second"), entries.size()) || pair.at("first") >= pair.at("second") ||
            pair.at("second") >= entries.size() ||
            !integer(pair.at("first_term"), native_access_row_capacity - 1) ||
            !integer(pair.at("second_term"), native_access_row_capacity - 1) ||
            !text(pair, "component") || !pair.at("id").is_number_unsigned() ||
            !one_of(pair.at("kind"), {"write_write", "read_write"}) ||
            !one_of(pair.at("scope"), {"same_entity", "possible_entities"}))
            throw std::runtime_error("Invalid native access overlap");
        const auto& left = entries.at(pair.at("first").get<std::size_t>())
                               .at("access")
                               .at("terms")
                               .at(pair.at("first_term").get<std::size_t>());
        const auto& right = entries.at(pair.at("second").get<std::size_t>())
                                .at("access")
                                .at("terms")
                                .at(pair.at("second_term").get<std::size_t>());
        const bool fixed = left.at("source") == "entity" && right.at("source") == "entity";
        const bool both_write = left.at("writes").get<bool>() && right.at("writes").get<bool>();
        if (left.at("analysis") != "included" || right.at("analysis") != "included" ||
            left.at("id") != pair.at("id") || right.at("id") != pair.at("id") ||
            left.at("name") != pair.at("component") ||
            pair.at("kind") != (both_write ? "write_write" : "read_write") ||
            pair.at("scope") != (fixed ? "same_entity" : "possible_entities") ||
            (fixed && left.at("source_id") != right.at("source_id")) ||
            (!left.at("writes").get<bool>() && !right.at("writes").get<bool>()))
            throw std::runtime_error("Native access overlap lost its declarations");
    }
}
} // namespace forge::detail
