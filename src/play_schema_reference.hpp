#pragma once
#include "asset_bytes.hpp"
#include <forge/content_digest.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace forge {
inline constexpr unsigned play_schema_reference_protocol = 1;

// A hint describes metadata already held by this client, never an authority
// for runtime reflection. Misses fall back to the current complete schema.
inline void validate_play_schema_hint(const nlohmann::json& hint, std::size_t limit) {
    const auto positive = [](const nlohmann::json& value) {
        return value.is_number_unsigned()
                   ? value.get<std::uint64_t>() > 0
                   : value.is_number_integer() && value.get<std::int64_t>() > 0;
    };
    if (!hint.is_object() || hint.size() != 3 || !hint.contains("sha256") ||
        !hint.at("sha256").is_string() ||
        !valid_content_digest(hint.at("sha256").get<std::string>()) || !hint.contains("world") ||
        !positive(hint.at("world")) || !hint.contains("bytes") || !positive(hint.at("bytes")) ||
        hint.at("bytes").get<std::uint64_t>() > limit)
        throw std::runtime_error("Invalid known_schema hint");
}
inline nlohmann::json play_schema_identity(const nlohmann::json& schema, std::uint64_t generation) {
    const auto encoded = schema.dump();
    return {{"sha256", asset_detail::content_digest(std::as_bytes(std::span(encoded)))},
            {"bytes", encoded.size()},
            {"world", generation}};
}
inline nlohmann::json play_schema_wire(const nlohmann::json& schema, std::uint64_t generation,
                                       const nlohmann::json& hint) {
    if (!hint.is_null() && hint.at("world").get<std::uint64_t>() == generation) {
        const auto identity = play_schema_identity(schema, generation);
        if (identity == hint)
            return {{"ref", identity.at("sha256")}, {"bytes", identity.at("bytes")}};
    }
    return schema;
}
// Keep the existing complete-response budget even when only its schema value
// travels by reference. Candidate admission always builds the complete value.
inline bool play_schema_response_fits(const nlohmann::json& response, std::size_t limit) {
    auto bytes = response.dump().size() + 1;
    if (bytes > limit)
        return false;
    const auto found = response.find("schema");
    if (found != response.end() && found->is_object() && found->contains("ref")) {
        bytes -= found->dump().size();
        return found->at("bytes").get<std::uint64_t>() <= limit - bytes;
    }
    return true;
}
// Resolve into the editor's existing schema, with only a bounded identity as
// derived state. A new session clears both; a new world must send a full schema.
inline void observe_play_schema(const nlohmann::json& value, std::uint64_t generation,
                                const nlohmann::json& sent_hint, bool negotiated,
                                nlohmann::json& schema, nlohmann::json& identity) {
    if (value.is_object() && value.contains("ref")) {
        if (!negotiated || value.size() != 2 || !value.at("ref").is_string() ||
            !value.contains("bytes") || !value.at("bytes").is_number_integer() ||
            identity.is_null() || schema.is_null() || sent_hint != identity ||
            identity.at("world").get<std::uint64_t>() != generation ||
            value.at("ref") != identity.at("sha256") || value.at("bytes") != identity.at("bytes"))
            throw std::runtime_error("Stale or invalid runtime schema reference");
        return;
    }
    auto next = negotiated && value.is_object() && generation
                    ? play_schema_identity(value, generation)
                    : nlohmann::json();
    schema = value;
    identity = std::move(next);
}
} // namespace forge
