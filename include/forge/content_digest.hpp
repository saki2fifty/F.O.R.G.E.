#pragma once
#include <string_view>
namespace forge {
// Shape of a lowercase SHA256 string; byte/structure admission remains separate.
bool valid_content_digest(std::string_view digest);
} // namespace forge
