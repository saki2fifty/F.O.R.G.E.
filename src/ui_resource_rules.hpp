#pragma once
#include <string>
#include <string_view>

namespace forge::ui_resource_detail {
bool image_extension(std::string_view lower_extension);
std::string join(const std::string& base, const std::string& resource);
} // namespace forge::ui_resource_detail
