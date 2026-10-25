#pragma once
#include <istream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace forge::detail {
// Callers retain ownership, byte limits and domain/schema validation.
// File streams must use binary mode so native text terminators cannot hide data.
inline nlohmann::json parse_json_document(std::istream& input) {
    auto result = nlohmann::json::parse(input);
    // The pinned stream adapter sets eofbit only at physical EOF. A raw NUL
    // terminates string-buffer parsing without consuming the complete file.
    if (!input.eof())
        throw std::runtime_error("JSON record did not consume the complete file");
    return result;
}
template <class Json = nlohmann::json>
inline Json parse_json_document(std::string_view text,
                                typename Json::parser_callback_t callback = nullptr) {
    if (text.find('\0') != std::string_view::npos)
        throw std::runtime_error("JSON record contains a raw NUL byte");
    return Json::parse(text, std::move(callback));
}
} // namespace forge::detail
