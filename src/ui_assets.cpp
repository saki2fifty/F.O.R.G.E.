#include "asset_bytes.hpp"
#include "texture_decode.h"
#include "ui_filter_profile.hpp"
#include "ui_font_profile.hpp"
#include "ui_gradient_profile.hpp"
#include "ui_localization_admission.hpp"
#include "ui_markup.hpp"
#include "ui_shadow_motion.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <forge/assets.hpp>
#include <forge/scene.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <limits>
#include <utility>
namespace forge {
namespace {
bool image_extension(std::string_view extension) {
    return extension == ".tga" || extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg";
}
void require(bool b, const char* msg) {
    if (!b)
        throw std::runtime_error(msg);
}
std::string lower(std::string s) {
    for (auto& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::uint32_t be(std::span<const std::byte> b, std::size_t p, unsigned n) {
    require(p <= b.size() && n <= b.size() - p, "Truncated UI resource");
    std::uint32_t v = 0;
    while (n--)
        v = (v << 8) | std::to_integer<unsigned>(b[p++]);
    return v;
}
unsigned le(std::span<const std::byte> b, std::size_t p) {
    return std::to_integer<unsigned>(b[p]) | (std::to_integer<unsigned>(b[p + 1]) << 8);
}
struct TextProfile {
    unsigned filters = 0;
    unsigned gradients = 0;
};
TextProfile validate_text_profile(std::string_view text, bool markup,
                                  std::vector<UiMarkupElement>* elements = nullptr,
                                  std::vector<UiStyleDeclaration>* declarations = nullptr) {
    require(text.size() <= 256 * 1024, "RML/RCSS exceeds 256 KiB");
    require(text.find('\0') == std::string_view::npos, "RML/RCSS contains NUL");
    (void)nlohmann::json(std::string(text)).dump();
    if (!markup)
        require(lower(std::string(text)).find("@import") == text.npos,
                "RCSS imports are deferred; link each stylesheet from RML");
    std::vector<std::string> stack;
    std::vector<std::size_t> element_stack;
    std::size_t nodes = 0;
    unsigned braces = 0, parens = 0;
    std::string property, token;
    unsigned filter_profile = 0;
    unsigned gradients = 0;
    std::string declaration_name;
    auto value_begin = std::string_view::npos;
    const auto finish_declaration = [&](std::size_t end) {
        if (value_begin != std::string_view::npos)
            declarations->push_back({declaration_name, {value_begin, end}});
        else
            require(declaration_name.empty(), "Inline style declaration requires a colon");
        declaration_name.clear();
        value_begin = std::string_view::npos;
    };
    for (std::size_t i = 0; i < text.size();) {
        if (markup && text.substr(i, 4) == "<!--") {
            auto e = text.find("-->", i + 4);
            require(e != text.npos, "Unterminated RML comment");
            i = e + 3;
            continue;
        }
        if (!markup && text.substr(i, 2) == "/*") {
            auto e = text.find("*/", i + 2);
            require(e != text.npos, "Unterminated RCSS comment");
            i = e + 2;
            continue;
        }
        if (markup && text[i] == '<') {
            const auto tag_begin = i;
            ++i;
            bool closing = i < text.size() && text[i] == '/';
            if (closing)
                ++i;
            const auto start = i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) ||
                                       text[i] == '-' || text[i] == '_'))
                ++i;
            require(i > start, "Unsupported RML declaration or malformed tag");
            auto tag = lower(std::string(text.substr(start, i - start)));
            if (!closing && tag == ui_shadow_detail::tag)
                filter_profile =
                    std::max(filter_profile, ui_filter_detail::shadow_motion_profile_version);
            require(tag != "script" && tag != "iframe" && tag != "object" && tag != "template",
                    "RML scripts, embedded objects and templates are not supported");
            require(++nodes <= 8192, "RML node limit exceeded");
            char quote = 0;
            auto end = i;
            for (; end < text.size(); ++end) {
                char c = text[end];
                if (quote) {
                    if (c == quote)
                        quote = 0;
                } else if (c == '\'' || c == '"')
                    quote = c;
                else if (c == '>')
                    break;
                else
                    require(c != '<', "Malformed RML attribute");
            }
            require(end < text.size() && !quote && end - i <= 8192, "Malformed/oversized RML tag");
            if (elements && !closing) {
                UiMarkupElement element;
                element.tag = tag;
                element.parent =
                    element_stack.empty() ? UiMarkupElement::no_parent : element_stack.back();
                element.source = element.opening = {tag_begin, end + 1};
                element.content = element.closing = {end + 1, end + 1};
                elements->push_back(std::move(element));
            }
            // Inline styles use the same bounded RCSS admission as linked styles.
            if (!closing) {
                auto attributes = text.substr(i, end - i);
                std::size_t a = 0;
                while (a < attributes.size()) {
                    while (a < attributes.size() && space(attributes[a]))
                        ++a;
                    auto begin = a;
                    while (a < attributes.size() &&
                           (std::isalnum(static_cast<unsigned char>(attributes[a])) ||
                            attributes[a] == '-' || attributes[a] == '_'))
                        ++a;
                    if (a == begin) {
                        ++a;
                        continue;
                    }
                    auto name = lower(std::string(attributes.substr(begin, a - begin)));
                    UiMarkupAttribute attribute;
                    if (elements) {
                        attribute.name = name;
                        attribute.source = {i + begin, i + a};
                    }
                    if (name.starts_with("data-style-") &&
                        std::find(ui_shadow_detail::properties.begin(),
                                  ui_shadow_detail::properties.end(),
                                  std::string_view(name).substr(11)) !=
                            ui_shadow_detail::properties.end())
                        filter_profile = std::max(filter_profile,
                                                  ui_filter_detail::shadow_motion_profile_version);
                    if (name == "data-style-filter")
                        filter_profile =
                            std::max(filter_profile, ui_filter_detail::shadow_profile_version);
                    if (name == "data-style-backdrop-filter")
                        filter_profile =
                            std::max(filter_profile, ui_filter_detail::backdrop_profile_version);
                    if (name == "data-style-box-shadow")
                        filter_profile =
                            std::max(filter_profile, ui_filter_detail::box_shadow_profile_version);
                    if (name == "data-style-mask-image") {
                        filter_profile =
                            std::max(filter_profile, ui_filter_detail::luminance_profile_version);
                        gradients = ui_gradient_detail::profile_version;
                    }
                    if (name == "data-style-decorator") {
                        gradients = ui_gradient_detail::profile_version;
                        filter_profile =
                            std::max(filter_profile, ui_filter_detail::luminance_profile_version);
                    }
                    while (a < attributes.size() && space(attributes[a]))
                        ++a;
                    if (a == attributes.size() || attributes[a] != '=') {
                        if (elements)
                            elements->back().attributes.push_back(std::move(attribute));
                        continue;
                    }
                    ++a;
                    while (a < attributes.size() && space(attributes[a]))
                        ++a;
                    if (a == attributes.size())
                        break;
                    char q = attributes[a];
                    attribute.has_value = true;
                    if (q != '\'' && q != '"') {
                        const auto value_begin = a;
                        while (a < attributes.size() && !space(attributes[a]))
                            ++a;
                        if (elements) {
                            const auto value_end =
                                a == attributes.size() && attributes.back() == '/' ? a - 1 : a;
                            attribute.source.end = i + value_end;
                            attribute.value = {i + value_begin, i + value_end};
                            elements->back().attributes.push_back(std::move(attribute));
                        }
                        require(name != "style", "Quote inline UI styles");
                        continue;
                    }
                    begin = ++a;
                    while (a < attributes.size() && attributes[a] != q)
                        ++a;
                    if (elements) {
                        attribute.quote = q;
                        attribute.source.end = i + std::min(a + 1, attributes.size());
                        attribute.value = {i + begin, i + a};
                        elements->back().attributes.push_back(std::move(attribute));
                    }
                    if (name == "style") {
                        const auto style = attributes.substr(begin, a - begin);
                        // Native attribute parsing expands character references.
                        // Encoded styles conservatively require the current filter kit.
                        if (style.find('&') != style.npos) {
                            filter_profile = ui_filter_detail::profile_version;
                            gradients = ui_gradient_detail::profile_version;
                        }
                        const auto nested = validate_text_profile(style, false);
                        filter_profile = std::max(filter_profile, nested.filters);
                        gradients = std::max(gradients, nested.gradients);
                    }
                    if (a < attributes.size())
                        ++a;
                }
            }
            if (closing) {
                for (auto k = i; k < end; ++k)
                    require(space(text[k]), "Closing RML tag has attributes");
                require(!stack.empty() && stack.back() == tag, "Unbalanced RML tags");
                if (elements) {
                    auto& element = elements->at(element_stack.back());
                    element.content.end = tag_begin;
                    element.closing = {tag_begin, end + 1};
                    element.source.end = end + 1;
                    element_stack.pop_back();
                }
                stack.pop_back();
            } else {
                const bool single = (end > i && text[end - 1] == '/') || tag == "link" ||
                                    tag == "meta" || tag == "img" || tag == "input" ||
                                    tag == "br" || tag == "hr";
                if (elements) {
                    elements->back().self_closing = single;
                    if (!single)
                        element_stack.push_back(elements->size() - 1);
                }
                if (!single) {
                    stack.push_back(tag);
                    require(stack.size() <= 32, "RML nesting exceeds 32");
                }
                if (tag == "style") {
                    auto close = text.find("</style>", end + 1);
                    require(close != text.npos, "Unterminated RML style");
                    const auto nested =
                        validate_text_profile(text.substr(end + 1, close - end - 1), false);
                    filter_profile = std::max(filter_profile, nested.filters);
                    gradients = std::max(gradients, nested.gradients);
                    i = close;
                    continue;
                }
            }
            i = end + 1;
            continue;
        }
        if (!markup) {
            char c = text[i];
            if (declarations) {
                require(c != '{' && c != '}', "Inline style cannot contain blocks");
                if (!parens && c == ';')
                    finish_declaration(i);
                else if (!parens && c == ':' && value_begin == text.npos) {
                    require(!declaration_name.empty(), "Inline style property name is empty");
                    value_begin = i + 1;
                } else if (value_begin == text.npos && !space(c)) {
                    require(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_',
                            "Unsupported inline style property name");
                    declaration_name += char(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            // Anonymous functions and named @decorator type declarations share
            // this evidence. Quoted ordinary text/comments stay excluded.
            if (((property == "decorator" || property == "mask-image" || property == "source") &&
                 (c == '(' || c == ';' || c == '}')) ||
                (c == '{' && !property.empty())) {
                if (token == "luminance")
                    filter_profile =
                        std::max(filter_profile, ui_filter_detail::luminance_profile_version);
                if (token == "radial-gradient" || token == "repeating-radial-gradient" ||
                    token == "conic-gradient" || token == "repeating-conic-gradient")
                    gradients = ui_gradient_detail::profile_version;
                else if (token == "linear-gradient" || token == "repeating-linear-gradient" ||
                         token == "horizontal-gradient" || token == "vertical-gradient" ||
                         token == "gradient")
                    gradients = std::max(gradients, ui_gradient_detail::basic_profile_version);
            }
            if (c == '(' && property == "filter" && token == "drop-shadow")
                filter_profile = std::max(filter_profile, ui_filter_detail::shadow_profile_version);
            // Retain identifier pieces across comments, as the stylesheet reader does.
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '-')
                token += char(std::tolower(static_cast<unsigned char>(c)));
            else if (!space(c) && c != ':')
                token.clear();
            if (c == ':') {
                property = std::exchange(token, {});
                if (property == "filter")
                    filter_profile =
                        std::max(filter_profile, ui_filter_detail::basic_profile_version);
                if (property == "backdrop-filter")
                    filter_profile =
                        std::max(filter_profile, ui_filter_detail::backdrop_profile_version);
                if (property == "mask-image")
                    filter_profile =
                        std::max(filter_profile, ui_filter_detail::mask_profile_version);
                if (property == "box-shadow")
                    filter_profile =
                        std::max(filter_profile, ui_filter_detail::box_shadow_profile_version);
                if (std::find(ui_shadow_detail::properties.begin(),
                              ui_shadow_detail::properties.end(),
                              property) != ui_shadow_detail::properties.end())
                    filter_profile =
                        std::max(filter_profile, ui_filter_detail::shadow_motion_profile_version);
                require(property != "font", "Use explicit font-family and font-size properties");
            }
            if (c == '{' || c == ';' || c == '}')
                property.clear();
            if (!property.empty() && (std::isdigit(static_cast<unsigned char>(c)) || c == '.') &&
                (i == 0 || (!std::isalnum(static_cast<unsigned char>(text[i - 1])) &&
                            text[i - 1] != '#' && text[i - 1] != '_' && text[i - 1] != '.'))) {
                double value = 0;
                auto result = std::from_chars(text.data() + i, text.data() + text.size(), value);
                if (result.ptr > text.data() + i) {
                    require(result.ec == std::errc{} && std::isfinite(value) &&
                                std::abs(value) <= 65536,
                            "RCSS numeric bound exceeded");
                    require(property != "font-size" || value <= 256,
                            "UI font-size exceeds 256 units");
                    i = std::size_t(result.ptr - text.data());
                    continue;
                }
            }

            if (c == '\'' || c == '"') {
                auto q = c;
                ++i;
                bool done = false;
                for (; i < text.size(); ++i) {
                    if (text[i] == '\\') {
                        require(i + 1 < text.size(), "Truncated RCSS escape");
                        ++i;
                        continue;
                    }
                    if (text[i] == q) {
                        done = true;
                        break;
                    }
                }
                require(done, "Unterminated RCSS string");
            } else if (c == '{') {
                require(++braces <= 16, "RCSS block nesting limit");
            } else if (c == '}') {
                require(braces > 0, "Unbalanced RCSS block");
                --braces;
            } else if (c == '(') {
                require(++parens <= 16, "RCSS expression nesting limit");
            } else if (c == ')') {
                require(parens > 0, "Unbalanced RCSS expression");
                --parens;
            }
        }
        ++i;
    }
    require(stack.empty() && !braces && !parens, "Truncated RML/RCSS structure");
    if (declarations)
        finish_declaration(text.size());
    if (markup)
        require(nodes > 0 && text.find("<rml") != text.npos && text.find("<body") != text.npos,
                "RML requires rml and body elements");
    return {filter_profile, gradients};
}
} // namespace
void validate_ui_text(std::string_view text, bool markup) {
    (void)validate_text_profile(text, markup);
}
std::vector<UiMarkupElement> inspect_ui_markup(std::string_view text) {
    std::vector<UiMarkupElement> result;
    (void)validate_text_profile(text, true, &result);
    return result;
}
std::vector<UiStyleDeclaration> inspect_ui_style(std::string_view text) {
    std::vector<UiStyleDeclaration> result;
    (void)validate_text_profile(text, false, nullptr, &result);
    return result;
}
void validate_ui_font(std::span<const std::byte> b) { (void)ui_font_detail::face_count(b); }
UiImage decode_ui_image(std::span<const std::byte> b) {
    require(b.size() <= 16 * 1024 * 1024 + 274, "UI image source exceeds byte budget");
    const bool png = b.size() >= 4 && b[0] == std::byte{0x89} && b[1] == std::byte{'P'} &&
                     b[2] == std::byte{'N'} && b[3] == std::byte{'G'};
    const bool jpeg = b.size() >= 2 && b[0] == std::byte{0xff} && b[1] == std::byte{0xd8};
    if (png || jpeg) {
        // The existing C boundary contains codec errors/longjmp and owns its allocation.
        ForgeImagePixels pixels{};
        struct Release {
            ForgeImagePixels& pixels;
            ~Release() { forge_image_pixels_free(&pixels); }
        } release{pixels};
        constexpr std::size_t limit = 2048ull * 2048 * 4;
        const auto decode = png ? forge_png_decode : forge_jpeg_decode;
        require(decode(b.data(), b.size(), 2048, limit, 0, &pixels) != 0, pixels.error);
        require(pixels.width && pixels.height && pixels.width <= 2048 && pixels.height <= 2048 &&
                    pixels.channels >= 1 && pixels.channels <= 4 &&
                    (pixels.component_bytes == 1 || pixels.component_bytes == 2) && pixels.pixels &&
                    pixels.stride ==
                        std::size_t(pixels.width) * pixels.channels * pixels.component_bytes &&
                    pixels.size == pixels.stride * pixels.height,
                "Unsupported UI image pixel layout");
        UiImage image{pixels.width, pixels.height,
                      std::vector<std::byte>(std::size_t(pixels.width) * pixels.height * 4)};
        auto sample = [&](std::size_t at) -> unsigned {
            if (pixels.component_bytes == 1)
                return pixels.pixels[at];
            std::uint16_t value;
            std::memcpy(&value, pixels.pixels + at * 2, sizeof(value));
            return (unsigned(value) + 128) / 257; // Host-endian PNG16 to display byte.
        };
        for (std::size_t i = 0; i < std::size_t(pixels.width) * pixels.height; ++i) {
            const auto src = i * pixels.channels;
            const unsigned alpha = pixels.channels == 2   ? sample(src + 1)
                                   : pixels.channels == 4 ? sample(src + 3)
                                                          : 255;
            for (unsigned channel = 0; channel != 3; ++channel) {
                const auto value = sample(src + (pixels.channels < 3 ? 0 : channel));
                image.rgba[i * 4 + channel] = std::byte((value * alpha + 127) / 255);
            }
            image.rgba[i * 4 + 3] = std::byte(alpha);
        }
        return image;
    }
    require(b.size() >= 18, "Truncated UI TGA image");
    require(b[1] == std::byte{0} && b[2] == std::byte{2},
            "UI images require uncompressed truecolor TGA");
    const auto bits = std::to_integer<unsigned>(b[16]), flags = std::to_integer<unsigned>(b[17]);
    const unsigned w = le(b, 12), h = le(b, 14), offset = 18 + std::to_integer<unsigned>(b[0]);
    require(w && h && w <= 2048 && h <= 2048 && (bits == 24 || bits == 32) && !(flags & 0xd0),
            "Unsupported UI image dimensions/layout");
    const std::size_t bytes = std::size_t(w) * h * (bits / 8);
    require(offset <= b.size() && bytes <= b.size() - offset, "Truncated UI image pixels");
    UiImage out{w, h, std::vector<std::byte>(std::size_t(w) * h * 4)};
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            auto src = offset + (std::size_t((flags & 32) ? y : h - 1 - y) * w + x) * (bits / 8);
            auto dst = (std::size_t(y) * w + x) * 4;
            auto a = bits == 32 ? std::to_integer<unsigned>(b[src + 3]) : 255u;
            for (unsigned c = 0; c < 3; ++c)
                out.rgba[dst + c] =
                    std::byte((std::to_integer<unsigned>(b[src + 2 - c]) * a + 127) / 255);
            out.rgba[dst + 3] = std::byte(a);
        }
    return out;
}
namespace {
std::pair<unsigned, unsigned> image_dimensions(std::span<const std::byte> bytes) {
    // These immutable bytes already passed full pixel admission in read().
    // Snapshotting needs metadata only; avoid decoding the bitmap a third time.
    const bool png = bytes.size() >= 8 && std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool jpeg =
        bytes.size() >= 2 && bytes[0] == std::byte{0xff} && bytes[1] == std::byte{0xd8};
    if (!png && !jpeg)
        return {le(bytes, 12), le(bytes, 14)};
    ForgeImagePixels pixels{};
    const auto decode = png ? forge_png_decode : forge_jpeg_decode;
    require(decode(bytes.data(), bytes.size(), 2048, 2048ull * 2048 * 4, 1, &pixels) != 0,
            pixels.error);
    return {pixels.width, pixels.height};
}
} // namespace
std::string UiResources::join(const std::string& base, const std::string& resource) const {
    require(!resource.empty() && resource.size() <= 1024 &&
                resource.find_first_of(":\\?#") == std::string::npos,
            "UI resources require project-relative local paths");
    const auto p = std::filesystem::u8path(resource);
    require(!p.is_absolute(), "Absolute UI resource path rejected");
    auto result = (std::filesystem::u8path(base).parent_path() / p).lexically_normal();
    return path_utf8(ProjectPaths::normalize(result));
}
const std::vector<std::byte>& UiResources::read(const std::string& name) {
    const auto locator = ProjectPaths::normalize(std::filesystem::u8path(name));
    const auto key = path_utf8(locator);
    if (auto it = files_.find(key); it != files_.end())
        return it->second.bytes;
    const auto ext = lower(locator.extension().string());
    return admit(locator,
                 access_.read(locator, ext == ".rml" || ext == ".rcss" ? 256 * 1024
                                                                       : 16 * 1024 * 1024 + 274));
}
const std::vector<std::byte>& UiResources::admit(const std::filesystem::path& locator,
                                                 std::vector<std::byte> data) {
    const auto key = path_utf8(locator);
    require(!files_.contains(key), "UI candidate sources are immutable");
    require(resource_count() < 32, "UI dependency count exceeds 32");
    const auto ext = lower(locator.extension().string());
    const bool text = ext == ".rml" || ext == ".rcss";
    require(text || ui_font_detail::extension(ext) || image_extension(ext),
            "Unsupported UI resource type");
    require(data.size() <= (text ? 256 * 1024 : 16 * 1024 * 1024 + 274),
            "UI source exceeds byte budget");
    require(bytes_ + data.size() <= 32 * 1024 * 1024, "UI resource bytes exceed 32 MiB");
    TextProfile profile;
    if (text)
        profile = validate_text_profile(
            std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
            ext == ".rml");
    else if (image_extension(ext)) {
        const bool png = data.size() >= 8 && std::memcmp(data.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
        const bool jpeg =
            data.size() >= 2 && data[0] == std::byte{0xff} && data[1] == std::byte{0xd8};
        require(ext == ".png"                     ? png
                : ext == ".jpg" || ext == ".jpeg" ? jpeg
                                                  : !png && !jpeg,
                "UI image signature does not match its source extension");
        (void)decode_ui_image(data);
    } else {
        validate_ui_font(data);
        require((ext != ".ttc" && ext != ".otc") || be(data, 0, 4) == 0x74746366,
                "UI collection font extension requires a collection header");
    }
    bytes_ += data.size();
    return files_.emplace(key, File{std::move(data), profile.filters, profile.gradients})
        .first->second.bytes;
}
std::filesystem::path UiResources::document_source(AssetRef<UiDocumentAsset> ref) const {
    const auto resolved = AssetCatalog::open_project(paths_.root()).resolve(ref);
    require(resolved.state == AssetState::Available, "UI document asset missing or wrong type");
    require(resolved.record->schema_version == 1, "Unsupported UI document asset schema");
    const auto source = ProjectPaths::normalize(resolved.record->source);
    require(source.extension() == ".rml", "UI asset must reference RML");
    (void)paths_.resolve(source);
    return source;
}
std::string UiResources::document(AssetRef<UiDocumentAsset> ref) {
    const auto source = document_source(ref);
    const auto name = path_utf8(source);
    (void)read(name);
    documents_[ref.id] = source;
    return name;
}
std::string UiResources::draft_document(AssetRef<UiDocumentAsset> ref,
                                        const std::filesystem::path& source,
                                        std::string_view text) {
    require(!access_.packaged(), "UI drafts require an editable project");
    require(files_.empty() && documents_.empty() && !localization_ && automatic_.empty(),
            "UI draft requires a fresh candidate");
    const auto registered = document_source(ref);
    require(paths_.same_locator(registered, source),
            "UI draft source does not match its registered document");
    require(text.size() <= 256 * 1024, "RML/RCSS exceeds 256 KiB");
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    (void)admit(registered, {bytes.begin(), bytes.end()});
    documents_[ref.id] = registered;
    return path_utf8(registered);
}
const UiLocalizationData& UiResources::localization(AssetRef<UiLocalizationAsset> ref) {
    if (localization_) {
        require(localization_->asset == ref.id, "One UI context supports one localization catalog");
        return *localization_;
    }
    require(resource_count() < 32, "UI dependency count exceeds 32");
    const auto catalog = AssetCatalog::open_project(paths_.root());
    const auto found = catalog.records().find(ref.id);
    require(found != catalog.records().end() && found->second.type == UiLocalizationAsset::type,
            "UI localization asset missing or wrong type");
    // A selected cooked asset does not require its editable source to exist.
    const auto& record = found->second;
    const auto key = record.metadata.at("forge.import").at("key").get<std::string>();
    DerivedDataCache cache(paths_.root(),
                           {ui_localization_byte_limit, ui_localization_byte_limit + 65536, 1});
    const auto artifact = cache.load_selected(key, [](const auto&) {});
    auto data = admit_ui_localization_artifact(record, artifact);
    const auto retained = data.resident_bytes();
    require(retained <= 32 * 1024 * 1024 - bytes_, "UI resource bytes exceed 32 MiB");
    localization_.emplace(std::move(data));
    bytes_ += retained;
    return *localization_;
}
UiAssetSnapshot UiResources::snapshot() const {
    UiAssetSnapshot result{paths_.root(), documents_, {}, automatic_};
    for (const auto& [name, file] : files_) {
        const auto& bytes = file.bytes;
        const auto source = std::filesystem::u8path(name);
        const auto extension = lower(source.extension().string());
        const std::string type = extension == ".rml"          ? "ui_document"
                                 : extension == ".rcss"       ? "ui_stylesheet"
                                 : image_extension(extension) ? "texture"
                                                              : "ui_font";
        UiSourceInfo info{source,
                          type,
                          asset_detail::content_digest(bytes),
                          bytes.size(),
                          {{"format", extension.substr(1)}}};
        Json features = Json::object();
        if (type == "ui_font")
            info.details.update(ui_font_detail::describe(bytes));
        if (type == "ui_font" && bytes.size() >= 4 && be(bytes, 0, 4) == 0x74746366) {
            features["font_collections"] = ui_font_detail::collection_profile_version;
        }
        if (file.filter_profile)
            features["filters"] = file.filter_profile;
        if (file.gradients)
            features["gradients"] = file.gradients;
        if (!features.empty())
            info.details["runtime_features"] = std::move(features);
        if (image_extension(extension)) {
            const auto [width, height] = image_dimensions(bytes);
            info.details["width"] = width;
            info.details["height"] = height;
        }
        result.sources.push_back(std::move(info));
    }
    return result;
}
void UiResources::automatic(const std::string& locator) {
    const auto path = ProjectPaths::normalize(std::filesystem::u8path(locator));
    (void)read(path_utf8(path));
    automatic_.insert(path);
}
AssetRecord register_ui_document(const std::filesystem::path& root,
                                 const std::filesystem::path& source) {
    UiResources candidate(root);
    auto path = ProjectPaths::normalize(source);
    require(path.extension() == ".rml", "Select a project-relative RML document");
    (void)candidate.read(path_utf8(path));
    const auto index = AssetCatalog::project_index(root);
    const auto baseline = std::filesystem::exists(index)
                              ? asset_detail::read_bytes(index, max_asset_index_bytes)
                              : std::vector<std::byte>{};
    auto catalog = AssetCatalog::open_project(root);
    for (const auto& [id, r] : catalog.records())
        if (ProjectPaths(root).same_locator(r.source, path)) {
            require(r.type == UiDocumentAsset::type, "Source is registered as another asset type");
            return r;
        }
    AssetRecord record{AssetId::generate(), UiDocumentAsset::type, path, 1, {}};
    catalog.add(record);
    const auto current = std::filesystem::exists(index)
                             ? asset_detail::read_bytes(index, max_asset_index_bytes)
                             : std::vector<std::byte>{};
    require(baseline == current, "Asset catalog changed; refresh and retry");
    catalog.save(index);
    return record;
}
} // namespace forge
namespace forge {
AssetRecord create_ui_example(const std::filesystem::path& root) {
    ProjectPaths paths(root);
    auto directory =
        std::filesystem::path("Assets/UI") / ("HUD-" + AssetId::generate().str().substr(0, 8));
    auto absolute = paths.resolve(directory);
    if (!std::filesystem::create_directories(absolute))
        throw std::runtime_error("UI example destination already exists");
    try {
        atomic_write(absolute / "hud.rml",
                     R"rml(<rml><head><link type="text/rcss" href="hud.rcss" /></head>
<body><div id="panel"><h1>FORGE runtime UI</h1><img src="badge.tga" width="32" height="32" />
<p>Simulation tick: {{tick}}</p><p>Paused: {{paused}}</p>
<button data-event-click="command('Pause')">Pause</button>
<button data-event-click="command('Resume')">Resume</button>
<button data-event-click="command('Step')">Step</button>
<p>Type here without moving gameplay:</p><input type="text" value="Player" />
<p>Escape releases input. F6 pauses/resumes. F7 steps while paused.</p></div></body></rml>)rml");
        atomic_write(
            absolute / "hud.rcss",
            R"(body { font-family:Lato; font-size:18px; color:#e5edf5; pointer-events:none; }
#panel { position:absolute; left:24px; top:24px; width:340px; padding:20px; background-color:#18232fee; border-radius:10px; pointer-events:auto; }
h1 { display:block; font-size:24px; margin-bottom:12px; } p { display:block; margin:10px 0; }
button { padding:8px 12px; margin-right:6px; background-color:#34597b; border-radius:4px; }
button:hover { background-color:#477da9; } input { display:block; width:300px; height:30px; background-color:#101922; padding:4px; })");
        std::vector<std::byte> image(18 + 4 * 4 * 4, std::byte{});
        image[2] = std::byte{2};
        image[12] = image[14] = std::byte{4};
        image[16] = std::byte{32};
        image[17] = std::byte{40};
        for (std::size_t i = 18; i < image.size(); i += 4) {
            image[i] = std::byte{210};
            image[i + 1] = std::byte{160};
            image[i + 2] = std::byte{70};
            image[i + 3] = std::byte{255};
        }
        std::ofstream out(absolute / "badge.tga", std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
        out.close();
        if (!out)
            throw std::runtime_error("UI example image write failed");
        return register_ui_document(root, directory / "hud.rml");
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(absolute, ec);
        throw;
    }
}
} // namespace forge
