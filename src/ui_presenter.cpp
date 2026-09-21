#include "asset_bytes.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <forge/ui_presenter.hpp>
#include <set>
#include <thread>
namespace forge {
namespace {
bool presenter_active = false; // Single presentation owner/thread in this phase.
using Json = nlohmann::json;
} // namespace
struct UiPresenter::Impl final : Rml::SystemInterface, Rml::FileInterface {
    struct Doc {
        std::string instance, model_name;
        Json descriptor;
        Rml::ElementDocument* document = nullptr;
        Rml::DataModelHandle model;
    };
    struct Set {
        std::string name;
        Rml::Context* context = nullptr;
        std::shared_ptr<UiResources> resources;
        UiAssetSnapshot assets;
        std::vector<std::unique_ptr<Doc>> docs;
    };
    struct File {
        const std::vector<std::byte>* bytes;
        std::size_t pos = 0;
    };
    // Prepare geometry/textures against a candidate without touching the host framebuffer.
    // The same manager owns the prepared resources after publication.
    struct RenderGate final : Rml::RenderInterface {
        Rml::RenderInterface& target;
        std::string& error;
        bool preparing = false;
        RenderGate(Rml::RenderInterface& r, std::string& e) : target(r), error(e) {}
        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                    Rml::Span<const int> i) override {
            auto h = target.CompileGeometry(v, i);
            if (!h)
                error = "UI geometry allocation/admission failed";
            return h;
        }
        void ReleaseGeometry(Rml::CompiledGeometryHandle h) override { target.ReleaseGeometry(h); }
        void RenderGeometry(Rml::CompiledGeometryHandle h, Rml::Vector2f p,
                            Rml::TextureHandle t) override {
            if (!preparing)
                target.RenderGeometry(h, p, t);
        }
        Rml::TextureHandle LoadTexture(Rml::Vector2i& d, const Rml::String& p) override {
            auto h = target.LoadTexture(d, p);
            if (!h)
                error = "UI image could not be loaded: " + p;
            return h;
        }
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> b, Rml::Vector2i d) override {
            auto h = target.GenerateTexture(b, d);
            if (!h)
                error = "UI generated texture allocation/admission failed";
            return h;
        }
        void ReleaseTexture(Rml::TextureHandle h) override { target.ReleaseTexture(h); }
        void EnableScissorRegion(bool b) override {
            if (!preparing)
                target.EnableScissorRegion(b);
        }
        void SetScissorRegion(Rml::Rectanglei r) override {
            if (!preparing)
                target.SetScissorRegion(r);
        }
        void SetTransform(const Rml::Matrix4f* m) override {
            if (!preparing)
                target.SetTransform(m);
        }
        void EnableClipMask(bool b) override {
            if (!preparing)
                target.EnableClipMask(b);
        }
        void RenderToClipMask(Rml::ClipMaskOperation o, Rml::CompiledGeometryHandle h,
                              Rml::Vector2f p) override {
            if (!preparing)
                target.RenderToClipMask(o, h, p);
        }
        Rml::LayerHandle PushLayer() override {
            error = "UI offscreen layers/filters are not supported in this integration";
            return 0;
        }
        Rml::CompiledFilterHandle CompileFilter(const Rml::String&,
                                                const Rml::Dictionary&) override {
            error = "UI filters are not supported in this integration";
            return 0;
        }
        Rml::CompiledShaderHandle CompileShader(const Rml::String&,
                                                const Rml::Dictionary&) override {
            error = "UI shader decorators are not supported in this integration";
            return 0;
        }
    };
    std::string session, error, loading_prefix, failed_structure;
    RenderGate renderer;
    UiPlatformCallbacks platform;
    std::set<int> held_keys;
    std::set<std::string> admitted_fonts;
    std::size_t font_bytes = 0;
    std::filesystem::path project;
    std::vector<std::byte> font;
    std::map<std::string, std::shared_ptr<UiResources>> resources;
    std::map<Rml::FileHandle, std::unique_ptr<File>> files;
    std::unique_ptr<Set> live;
    std::uint64_t presentation_revision = 0;
    std::deque<Json> pending;
    ui_protocol::Replica replica;
    Json current;
    std::thread::id thread = std::this_thread::get_id();
    std::uint64_t serial = 0, command_id = 0, generation = 0;
    double time = 0;
    int width = 1280, height = 720;
    float density = 1;
    bool initialized = false;
    void check() const {
        if (std::this_thread::get_id() != thread)
            throw std::logic_error("UI presenter used off its owning thread");
    }
    Impl(Rml::RenderInterface& r, std::filesystem::path p, std::vector<std::byte> f,
         UiPlatformCallbacks hooks, Rml::TextInputHandler* text_handler)
        : renderer(r, error), platform(std::move(hooks)), project(std::move(p)),
          font(std::move(f)) {
        if (presenter_active)
            throw std::runtime_error(
                "Only one runtime UI presenter is supported per presentation host");
        validate_ui_font(font);
        presenter_active = true;
        try {
            Rml::SetSystemInterface(this);
            Rml::SetFileInterface(this);
            Rml::SetRenderInterface(&renderer);
            Rml::SetTextInputHandler(text_handler);
            if (!Rml::Initialise())
                throw std::runtime_error("RmlUi initialization failed");
            initialized = true;
            if (!Rml::LoadFontFace({reinterpret_cast<const Rml::byte*>(font.data()), font.size()},
                                   "Lato", Rml::Style::FontStyle::Normal,
                                   Rml::Style::FontWeight::Normal, true))
                throw std::runtime_error("Default UI font could not be loaded");
        } catch (...) {
            if (initialized)
                Rml::Shutdown();
            Rml::SetSystemInterface(nullptr);
            Rml::SetFileInterface(nullptr);
            Rml::SetRenderInterface(nullptr);
            Rml::SetTextInputHandler(nullptr);
            presenter_active = false;
            throw;
        }
    }
    ~Impl() {
        retire(live);
        if (initialized)
            Rml::Shutdown();
        files.clear();
        resources.clear();
        Rml::SetSystemInterface(nullptr);
        Rml::SetFileInterface(nullptr);
        Rml::SetRenderInterface(nullptr);
        Rml::SetTextInputHandler(nullptr);
        presenter_active = false;
    }
    double GetElapsedTime() override { return time; }
    void SetClipboardText(const Rml::String& text) override {
        if (platform.set_clipboard && text.size() <= 4096)
            platform.set_clipboard(text);
    }
    void GetClipboardText(Rml::String& out) override {
        if (platform.get_clipboard) {
            out = platform.get_clipboard();
            if (out.size() > 4096)
                out.clear();
        }
    }
    void ActivateKeyboard(Rml::Vector2f pos, float height) override {
        if (platform.activate_text)
            platform.activate_text(pos.x, pos.y, height);
    }
    void DeactivateKeyboard() override {
        if (platform.deactivate_text)
            platform.deactivate_text();
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (error.empty() && (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ||
                              type == Rml::Log::LT_WARNING))
            error = message.substr(0, 2048);
        return true;
    }
    void JoinPath(Rml::String& out, const Rml::String& base, const Rml::String& path) override {
        try {
            auto slash = base.find('/');
            if (slash == base.npos)
                throw std::runtime_error("UI resource has no admitted document origin");
            const auto prefix = base.substr(0, slash);
            out = prefix + "/" + resources.at(prefix)->join(base.substr(slash + 1), path);
        } catch (const std::exception& e) {
            error = e.what();
            out = "invalid/blocked";
        }
    }
    Rml::FileHandle Open(const Rml::String& requested) override {
        auto path = requested;
        try {
            // RmlUi 6.3 @font-face passes src directly to FileInterface, without
            // SystemInterface::JoinPath. Its font locators are project-relative.
            auto prefix = path.substr(0, path.find('/'));
            if (!resources.contains(prefix) && (path.ends_with(".ttf") || path.ends_with(".otf"))) {
                if (loading_prefix.empty())
                    throw std::runtime_error("Font load outside candidate admission");
                path = loading_prefix + "/" +
                       path_utf8(ProjectPaths::normalize(std::filesystem::u8path(path)));
            }
            auto slash = path.find('/');
            if (slash == path.npos)
                throw std::runtime_error("UI resource has no candidate origin");
            auto& bytes = resources.at(path.substr(0, slash))->read(path.substr(slash + 1));
            if (path.ends_with(".ttf") || path.ends_with(".otf")) {
                auto digest = asset_detail::content_digest(bytes);
                if (!admitted_fonts.contains(digest)) {
                    if (admitted_fonts.size() >= 32 || font_bytes + bytes.size() > 16 * 1024 * 1024)
                        throw std::runtime_error("UI font lifetime budget exceeded; restart Play");
                    admitted_fonts.insert(std::move(digest));
                    font_bytes += bytes.size();
                }
            }
            if (files.size() >= 32)
                throw std::runtime_error("Too many open UI resources");
            auto f = std::make_unique<File>();
            f->bytes = &bytes;
            auto handle = reinterpret_cast<Rml::FileHandle>(f.get());
            files.emplace(handle, std::move(f));
            return handle;
        } catch (const std::exception& e) {
            error = e.what();
            return 0;
        }
    }
    void Close(Rml::FileHandle h) override { files.erase(h); }
    size_t Read(void* out, size_t n, Rml::FileHandle h) override {
        auto it = files.find(h);
        if (it == files.end())
            return 0;
        auto& f = *it->second;
        n = std::min(n, f.bytes->size() - f.pos);
        if (n)
            std::memcpy(out, f.bytes->data() + f.pos, n);
        f.pos += n;
        return n;
    }
    bool Seek(Rml::FileHandle h, long offset, int origin) override {
        auto it = files.find(h);
        if (it == files.end())
            return false;
        auto& f = *it->second;
        std::int64_t base = origin == SEEK_SET   ? 0
                            : origin == SEEK_CUR ? std::int64_t(f.pos)
                            : origin == SEEK_END ? std::int64_t(f.bytes->size())
                                                 : -1;
        if (base < 0 || offset < -base || offset > std::int64_t(f.bytes->size()) - base)
            return false;
        f.pos = std::size_t(base + offset);
        return true;
    }
    size_t Tell(Rml::FileHandle h) override { return files.at(h)->pos; }
    size_t Length(Rml::FileHandle h) override { return files.at(h)->bytes->size(); }
    void retire(std::unique_ptr<Set>& set) {
        if (!set)
            return;
        Rml::RemoveContext(set->name);
        Rml::ReleaseRenderManagers();
        resources.erase(set->name);
        set.reset();
    }
    void enqueue(Doc& doc, const Rml::VariantList& args) {
        if (!live || args.empty() || args.size() > 2 || pending.size() >= 128)
            return;
        if (std::none_of(live->docs.begin(), live->docs.end(),
                         [&](const auto& d) { return d.get() == &doc; }))
            return;
        const auto name = args[0].Get<Rml::String>();
        const auto& allowed = doc.descriptor.at("commands");
        if (std::find(allowed.begin(), allowed.end(), Json(name)) == allowed.end()) {
            error = "UI command is not registered: " + name;
            return;
        }
        Json request = {{"version", ui_protocol::version}, {"session", session},
                        {"generation", generation},        {"id", ++command_id},
                        {"instance", doc.instance},        {"command", name}};
        if (args.size() == 2) {
            auto value = args[1].Get<Rml::String>();
            try {
                ui_protocol::validate_value(value);
            } catch (const std::exception& e) {
                error = e.what();
                --command_id;
                return;
            }
            request["value"] = value;
        }
        pending.push_back(std::move(request));
    }
    bool rebuild() {
        auto candidate = std::make_unique<Set>();
        candidate->name = "ui" + std::to_string(++serial);
        candidate->resources = std::make_shared<UiResources>(project);
        resources[candidate->name] = candidate->resources;
        error.clear();
        loading_prefix = candidate->name;
        try {
            Rml::Factory::ClearStyleSheetCache();
            Rml::Factory::ClearTemplateCache();
            candidate->context = Rml::CreateContext(candidate->name, {width, height}, &renderer);
            if (!candidate->context)
                throw std::runtime_error("UI context creation failed");
            candidate->context->SetDensityIndependentPixelRatio(density);
            auto descriptors = current.at("documents").get<std::vector<Json>>();
            std::stable_sort(
                descriptors.begin(), descriptors.end(),
                [](const auto& a, const auto& b) { return a.at("layer") < b.at("layer"); });
            for (const auto& desc : descriptors) {
                auto d = std::make_unique<Doc>();
                d->instance = desc.at("instance");
                d->descriptor = desc;
                d->model_name = "forge" + std::to_string(candidate->docs.size());
                auto model = candidate->context->CreateDataModel(d->model_name);
                if (!model)
                    throw std::runtime_error("UI data model creation failed");
                auto* ptr = d.get();
                for (const auto& [key, value] : desc.at("model").items()) {
                    (void)value;
                    if (!model.BindFunc(key, [ptr, key](Rml::Variant& out) {
                            const auto& v = ptr->descriptor.at("model").at(key);
                            if (v.is_boolean())
                                out = v.get<bool>();
                            else if (v.is_number_float())
                                out = v.get<double>();
                            else if (v.is_number_unsigned())
                                out = v.get<std::uint64_t>();
                            else if (v.is_number_integer())
                                out = v.get<std::int64_t>();
                            else
                                out = v.get<std::string>();
                        }))
                        throw std::runtime_error("UI variable binding failed");
                }
                model.BindEventCallback(
                    "command", [this, ptr](Rml::DataModelHandle, Rml::Event&,
                                           const Rml::VariantList& args) { enqueue(*ptr, args); });
                d->model = model.GetModelHandle();
                auto source = candidate->resources->document({desc.at("asset").get<AssetId>()});
                const auto& bytes = candidate->resources->read(source);
                std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (text.find("data-model") != std::string::npos)
                    throw std::runtime_error(
                        "FORGE supplies the document data model; omit data-model attributes");
                auto body = text.find("<body");
                if (body == std::string::npos)
                    throw std::runtime_error("UI document needs body");
                text.insert(body + 5, " data-model=\"" + d->model_name + "\"");
                d->document = candidate->context->LoadDocumentFromMemory(text, candidate->name +
                                                                                   "/" + source);
                if (!d->document)
                    throw std::runtime_error("UI document load failed");
                d->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
                candidate->docs.push_back(std::move(d));
            }
            candidate->context->Update();
            renderer.preparing = true;
            candidate->context->Render();
            renderer.preparing = false;
            for (auto& d : candidate->docs)
                if (!d->descriptor.at("visible").get<bool>())
                    d->document->Hide();
            if (!error.empty())
                throw std::runtime_error(error);
            candidate->assets = candidate->resources->snapshot();
            held_keys.clear();
            retire(live);
            live = std::move(candidate);
            ++presentation_revision;
            loading_prefix.clear();
            return true;
        } catch (const std::exception& e) {
            renderer.preparing = false;
            loading_prefix.clear();
            const std::string failure = e.what();
            retire(candidate);
            error = failure;
            return false;
        }
    }
};
UiPresenter::UiPresenter(Rml::RenderInterface& r, std::filesystem::path p,
                         std::vector<std::byte> font, UiPlatformCallbacks hooks,
                         Rml::TextInputHandler* text_handler)
    : impl_(std::make_unique<Impl>(r, std::move(p), std::move(font), std::move(hooks),
                                   text_handler)) {}
UiPresenter::~UiPresenter() = default;
const UiAssetSnapshot* UiPresenter::asset_snapshot() const {
    impl_->check();
    return impl_->live ? &impl_->live->assets : nullptr;
}
void UiPresenter::reset(std::string session, std::uint64_t generation) {
    auto& s = *impl_;
    s.check();
    s.retire(s.live);
    s.held_keys.clear();
    s.pending.clear();
    s.command_id = 0;
    s.session = std::move(session);
    s.generation = generation;
    s.current = nullptr;
    s.error.clear();
    s.failed_structure.clear();
    s.replica.reset(s.session, generation);
}
bool UiPresenter::accept(const Json& j) {
    auto& s = *impl_;
    s.check();
    if (!s.replica.accept(j))
        return false;
    bool rebuild = !s.live;
    const auto& docs = j.at("documents");
    if (s.live) {
        rebuild = s.live->docs.size() != docs.size();
        for (const auto& d : s.live->docs) {
            auto it = std::find_if(docs.begin(), docs.end(),
                                   [&](const auto& v) { return v.at("instance") == d->instance; });
            if (it == docs.end()) {
                rebuild = true;
                break;
            }
            if (it->at("layer") != d->descriptor.at("layer") ||
                it->at("commands") != d->descriptor.at("commands") ||
                it->at("model").size() != d->descriptor.at("model").size()) {
                rebuild = true;
                break;
            }
            for (const auto& [k, v] : it->at("model").items()) {
                (void)v;
                if (!d->descriptor.at("model").contains(k))
                    rebuild = true;
            }
        }
    }
    s.current = j;
    if (rebuild) {
        auto structure = docs;
        for (auto& desc : structure) {
            Json keys = Json::array();
            for (const auto& [key, value] : desc.at("model").items()) {
                (void)value;
                keys.push_back(key);
            }
            desc["model"] = std::move(keys);
        }
        const auto signature = structure.dump();
        if (signature == s.failed_structure)
            return false;
        const bool ok = s.rebuild();
        s.failed_structure = ok ? std::string{} : signature;
        return ok;
    }
    bool visibility_changed = false;
    for (const auto& d : s.live->docs)
        for (const auto& desc : docs)
            if (desc.at("instance") == d->instance &&
                desc.at("visible") != d->descriptor.at("visible"))
                visibility_changed = true;
    if (visibility_changed) {
        release_input();
        ++s.presentation_revision;
    }
    for (auto& d : s.live->docs) {
        auto it = std::find_if(docs.begin(), docs.end(),
                               [&](const auto& v) { return v.at("instance") == d->instance; });
        d->descriptor = *it;
        d->model.DirtyAllVariables();
        if (it->at("visible").get<bool>())
            d->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        else
            d->document->Hide();
    }
    return true;
}
bool UiPresenter::reload() {
    auto& s = *impl_;
    s.check();
    if (s.current.is_null())
        return false;
    bool ok = s.rebuild();
    if (ok)
        s.failed_structure.clear();
    return ok;
}
void UiPresenter::update(double t, int w, int h, float dp) {
    auto& s = *impl_;
    s.check();
    if (!std::isfinite(t) || t < s.time || w < 1 || h < 1 || w > 8192 || h > 8192 ||
        !std::isfinite(dp) || dp < .5f || dp > 4)
        throw std::runtime_error("Invalid UI presentation dimensions/time");
    s.time = t;
    s.width = w;
    s.height = h;
    s.density = dp;
    if (s.live) {
        s.live->context->SetDimensions({w, h});
        s.live->context->SetDensityIndependentPixelRatio(dp);
        s.live->context->Update();
    }
}
void UiPresenter::render() {
    auto& s = *impl_;
    s.check();
    if (s.live)
        s.live->context->Render();
}
bool UiPresenter::mouse_move(int x, int y, int m) {
    auto& s = *impl_;
    s.check();
    return s.live && !s.live->context->ProcessMouseMove(x, y, m);
}
bool UiPresenter::mouse_button(int b, bool down, int m) {
    auto& s = *impl_;
    s.check();
    if (!s.live)
        return false;
    return !(down ? s.live->context->ProcessMouseButtonDown(b, m)
                  : s.live->context->ProcessMouseButtonUp(b, m));
}
bool UiPresenter::wheel(float x, float y, int m) {
    auto& s = *impl_;
    s.check();
    return s.live && !s.live->context->ProcessMouseWheel({x, y}, m);
}
bool UiPresenter::key(int k, bool down, int m) {
    auto& s = *impl_;
    s.check();
    if (!s.live)
        return false;
    if (down)
        s.held_keys.insert(k);
    else
        s.held_keys.erase(k);
    const bool capture = wants_text();
    return !(down ? s.live->context->ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(k), m)
                  : s.live->context->ProcessKeyUp(static_cast<Rml::Input::KeyIdentifier>(k), m)) ||
           capture;
}
bool UiPresenter::text(const std::string& t) {
    auto& s = *impl_;
    s.check();
    try {
        ui_protocol::validate_value(t);
    } catch (const std::exception& e) {
        s.error = e.what();
        return true;
    }
    return s.live && !s.live->context->ProcessTextInput(t);
}
bool UiPresenter::wants_text() const {
    auto& s = *impl_;
    s.check();
    if (!s.live)
        return false;
    auto* e = s.live->context->GetFocusElement();
    return e && (e->GetTagName() == "input" || e->GetTagName() == "textarea");
}
void UiPresenter::release_input() {
    auto& s = *impl_;
    s.check();
    if (!s.live)
        return;
    auto* c = s.live->context;
    for (int k : s.held_keys)
        c->ProcessKeyUp(static_cast<Rml::Input::KeyIdentifier>(k), 0);
    s.held_keys.clear();
    if (auto* e = c->GetFocusElement())
        e->Blur();
    c->ProcessMouseLeave();
    for (int b = 0; b < 5; ++b)
        c->ProcessMouseButtonUp(b, 0);
}
std::optional<Json> UiPresenter::pending_command() const {
    auto& s = *impl_;
    s.check();
    return s.pending.empty() ? std::nullopt : std::optional<Json>(s.pending.front());
}
void UiPresenter::acknowledge(const Json& ack) {
    auto& s = *impl_;
    s.check();
    if (s.pending.empty() || ack.value("version", 0) != ui_protocol::version ||
        ack.value("session", "") != s.session ||
        ack.value("generation", std::uint64_t{}) != s.generation ||
        ack.at("id") != s.pending.front().at("id"))
        return;
    if (!ack.at("ok").get<bool>())
        s.error = ack.value("error", "UI command rejected");
    s.pending.pop_front();
}
const std::string& UiPresenter::diagnostic() const { return impl_->error; }
std::uint64_t UiPresenter::presentation_revision() const { return impl_->presentation_revision; }
std::size_t UiPresenter::document_count() const {
    return impl_->live ? impl_->live->docs.size() : 0;
}
} // namespace forge
