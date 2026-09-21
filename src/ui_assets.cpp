#include "asset_bytes.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/scene.hpp>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <limits>
namespace forge {
namespace {
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
} // namespace
void validate_ui_text(std::string_view text, bool markup) {
    require(text.size() <= 256 * 1024, "RML/RCSS exceeds 256 KiB");
    require(text.find('\0') == std::string_view::npos, "RML/RCSS contains NUL");
    (void)nlohmann::json(std::string(text)).dump();
    if (!markup)
        require(lower(std::string(text)).find("@import") == text.npos,
                "RCSS imports are deferred; link each stylesheet from RML");
    std::vector<std::string> stack;
    std::size_t nodes = 0;
    unsigned braces = 0, parens = 0;
    std::string property;
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
                    while (a < attributes.size() && space(attributes[a]))
                        ++a;
                    if (a == attributes.size() || attributes[a] != '=')
                        continue;
                    ++a;
                    while (a < attributes.size() && space(attributes[a]))
                        ++a;
                    if (a == attributes.size())
                        break;
                    char q = attributes[a];
                    if (q != '\'' && q != '"') {
                        while (a < attributes.size() && !space(attributes[a]))
                            ++a;
                        require(name != "style", "Quote inline UI styles");
                        continue;
                    }
                    begin = ++a;
                    while (a < attributes.size() && attributes[a] != q)
                        ++a;
                    if (name == "style")
                        validate_ui_text(attributes.substr(begin, a - begin), false);
                    if (a < attributes.size())
                        ++a;
                }
            }
            if (closing) {
                for (auto k = i; k < end; ++k)
                    require(space(text[k]), "Closing RML tag has attributes");
                require(!stack.empty() && stack.back() == tag, "Unbalanced RML tags");
                stack.pop_back();
            } else {
                const bool single = (end > i && text[end - 1] == '/') || tag == "link" ||
                                    tag == "meta" || tag == "img" || tag == "input" ||
                                    tag == "br" || tag == "hr";
                if (!single) {
                    stack.push_back(tag);
                    require(stack.size() <= 32, "RML nesting exceeds 32");
                }
                if (tag == "style") {
                    auto close = text.find("</style>", end + 1);
                    require(close != text.npos, "Unterminated RML style");
                    validate_ui_text(text.substr(end + 1, close - end - 1), false);
                    i = close;
                    continue;
                }
            }
            i = end + 1;
            continue;
        }
        if (!markup) {
            char c = text[i];
            if (c == ':') {
                auto end = i;
                while (end > 0 && space(text[end - 1]))
                    --end;
                auto start = end;
                while (start > 0 && (std::isalpha(static_cast<unsigned char>(text[start - 1])) ||
                                     text[start - 1] == '-'))
                    --start;
                property = lower(std::string(text.substr(start, end - start)));
                require(property != "filter" && property != "backdrop-filter" &&
                            property != "mask-image" && property != "box-shadow",
                        "UI filters, masks and shadows are not supported yet");
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
    if (markup)
        require(nodes > 0 && text.find("<rml") != text.npos && text.find("<body") != text.npos,
                "RML requires rml and body elements");
}
void validate_ui_font(std::span<const std::byte> b) {
    require(b.size() >= 12 && b.size() <= 4 * 1024 * 1024, "Invalid UI font size");
    const auto signature = be(b, 0, 4);
    require(signature == 0x00010000 || signature == 0x4f54544f,
            "Only standalone TrueType/OpenType fonts are supported");
    const auto count = be(b, 4, 2);
    require(count > 0 && count <= 128 && 12 + count * 16 <= b.size(),
            "Invalid UI font table directory");
    for (unsigned i = 0; i < count; ++i) {
        auto p = 12 + i * 16;
        auto off = be(b, p + 8, 4), len = be(b, p + 12, 4);
        require(off <= b.size() && len <= b.size() - off, "Truncated UI font table");
    }
}
UiImage decode_ui_image(std::span<const std::byte> b) {
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
        return it->second;
    require(files_.size() < 32, "UI dependency count exceeds 32");
    const auto ext = lower(locator.extension().string());
    const bool text = ext == ".rml" || ext == ".rcss";
    require(text || ext == ".ttf" || ext == ".otf" || ext == ".tga",
            "Unsupported UI resource type");
    auto data = asset_detail::read_bytes(paths_.resolve(locator),
                                         text ? 256 * 1024 : 16 * 1024 * 1024 + 274);
    require(bytes_ + data.size() <= 32 * 1024 * 1024, "UI resource bytes exceed 32 MiB");
    if (text)
        validate_ui_text(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
                         ext == ".rml");
    else if (ext == ".tga")
        (void)decode_ui_image(data);
    else
        validate_ui_font(data);
    bytes_ += data.size();
    return files_.emplace(key, std::move(data)).first->second;
}
std::string UiResources::document(AssetRef<UiDocumentAsset> ref) {
    const auto resolved = AssetCatalog::open_project(paths_.root()).resolve(ref);
    require(resolved.state == AssetState::Available, "UI document asset missing or wrong type");
    require(resolved.record->schema_version == 1, "Unsupported UI document asset schema");
    auto name = path_utf8(resolved.record->source);
    require(std::filesystem::u8path(name).extension() == ".rml", "UI asset must reference RML");
    (void)read(name);
    documents_[ref.id] = resolved.record->source;
    return name;
}
UiAssetSnapshot UiResources::snapshot() const {
    UiAssetSnapshot result{paths_.root(), documents_, {}};
    for (const auto& [name, bytes] : files_) {
        const auto source = std::filesystem::u8path(name);
        const auto extension = lower(source.extension().string());
        const std::string type = extension == ".rml"    ? "ui_document"
                                 : extension == ".rcss" ? "ui_stylesheet"
                                 : extension == ".tga"  ? "texture"
                                                        : "ui_font";
        UiSourceInfo info{source,
                          type,
                          asset_detail::content_digest(bytes),
                          bytes.size(),
                          {{"format", extension.substr(1)}}};
        if (extension == ".tga") {
            const auto image = decode_ui_image(bytes);
            info.details["width"] = image.width;
            info.details["height"] = image.height;
        }
        result.sources.push_back(std::move(info));
    }
    return result;
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
