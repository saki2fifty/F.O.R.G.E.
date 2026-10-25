#pragma once
#include <istream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace forge::detail {
// Callers retain file ownership, byte limits and domain/schema validation.
// File streams must use binary mode so native text terminators cannot hide data.
inline nlohmann::json parse_json_document(std::istream& input) {
    auto result = nlohmann::json::parse(input);
    // The pinned stream adapter sets eofbit only at physical EOF. A raw NUL
    // terminates string-buffer parsing without consuming the complete file.
    if (!input.eof())
        throw std::runtime_error("JSON record did not consume the complete file");
    return result;
}
inline nlohmann::json parse_json_document(std::string_view text) {
    if (text.find('\0') != std::string_view::npos)
        throw std::runtime_error("JSON record contains a raw NUL byte");
    return nlohmann::json::parse(text);
}
} // namespace forge::detail
