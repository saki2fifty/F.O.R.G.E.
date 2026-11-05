#include "asset_bytes.hpp"
#include "ui_filter_profile.hpp"
#include "ui_luminance_mask.hpp"
#include "ui_shadow_motion.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <forge/ui_presenter.hpp>
#include <limits>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>
namespace forge {
namespace {
bool presenter_active = false; // Single presentation owner/thread in this phase.
using Json = nlohmann::json;
Json localization_identity(const Json& descriptor) {
    return descriptor.is_null()
               ? Json()
               : Json{{"entity", descriptor.at("entity")}, {"asset", descriptor.at("asset")}};
}
Json application_state_identity(const Json& descriptor) {
    return descriptor.is_null() ? Json()
                                : Json{{"controller", descriptor.at("controller")},
                                       {"source_asset", descriptor.at("source_asset")},
                                       {"definition_digest", descriptor.at("definition_digest")}};
}
} // namespace
struct UiPresenter::Impl final : Rml::SystemInterface, Rml::FileInterface {
    struct Doc {
        std::string instance, model_name;
        Json descriptor;
        Rml::ElementDocument* document = nullptr;
        Rml::Context* context = nullptr;
        Rml::DataModelHandle model;
    };
    struct Set {
        std::string name;
        Rml::Context* context = nullptr;
        std::map<AssetId, Rml::Context*> outputs;
        std::shared_ptr<UiResources> resources;
        UiAssetSnapshot assets;
        Json localization;
        Json application_states;
        const UiLocalizationData* messages = nullptr;
        std::vector<std::unique_ptr<Doc>> docs;
        Rml::ElementDocument* frame_document = nullptr;
        Rml::ElementText* frame_text = nullptr;
    };
    std::set<AssetId> prepared_outputs;
    struct File {
        const std::vector<std::byte>* bytes;
        std::size_t pos = 0;
    };
    struct Prepared {
        std::unique_ptr<Set> set;
        Json state;
        ui_protocol::Replica replica;
        std::string session;
        std::uint64_t generation = 0, ticket = 0;
    };
    // Prepare geometry/textures against a candidate without touching the host framebuffer.
    // The same manager owns the prepared resources after publication.
    struct RenderGate final : Rml::RenderInterface {
        Rml::RenderInterface& target;
        std::string& error;
        bool preparing = false;
        struct Layer {
            bool forwarded, valid;
        };
        std::vector<Layer> layers;
        Rml::LayerHandle virtual_layer = 1;
        RenderGate(Rml::RenderInterface& r, std::string& e) : target(r), error(e) {}
        void fail(std::string_view message) {
            if (error.empty())
                error.assign(message.substr(0, 2048));
        }
        template <class F> auto call(F action) -> std::invoke_result_t<F> {
            try {
                if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
                    action();
                    return;
                } else {
                    return action();
                }
            } catch (const std::exception& e) {
                fail(e.what());
            } catch (...) {
                fail("Unknown UI rendering callback failure");
            }
            if constexpr (!std::is_void_v<std::invoke_result_t<F>>)
                return {};
        }
        // Keep native render walks balanced when a backend allocation fails.
        // Report a new fault after the walk, while preserving an older source
        // diagnostic when the usable predecessor rendered successfully.
        template <class F> void checked(F action, bool propagate = true) {
            auto previous = std::move(error);
            error.clear();
            try {
                action();
                if (!error.empty())
                    throw std::runtime_error(error);
            } catch (const std::exception& e) {
                fail(e.what());
                if (propagate)
                    throw;
                return;
            }
            error = std::move(previous);
        }
        bool blocked() const {
            return std::any_of(layers.begin(), layers.end(),
                               [](const auto& layer) { return !layer.valid; });
        }
        void render(Rml::Context& context, bool propagate = true) {
            checked(
                [&] {
                    context.Render();
                    if (!layers.empty())
                        fail("UI native filter layer walk is unbalanced");
                },
                propagate);
        }
        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                    Rml::Span<const int> i) override {
            const auto h = call([&] { return target.CompileGeometry(v, i); });
            if (!h)
                fail("UI geometry allocation/admission failed");
            return h;
        }
        void ReleaseGeometry(Rml::CompiledGeometryHandle h) override {
            call([&] { target.ReleaseGeometry(h); });
        }
        void RenderGeometry(Rml::CompiledGeometryHandle h, Rml::Vector2f p,
                            Rml::TextureHandle t) override {
            if (!preparing && !blocked())
                call([&] { target.RenderGeometry(h, p, t); });
        }
        Rml::TextureHandle LoadTexture(Rml::Vector2i& d, const Rml::String& p) override {
            const auto h = call([&] { return target.LoadTexture(d, p); });
            if (!h)
                fail("UI image could not be loaded: " + p);
            return h;
        }
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> b, Rml::Vector2i d) override {
            const auto h = call([&] { return target.GenerateTexture(b, d); });
            if (!h)
                fail("UI generated texture allocation/admission failed");
            return h;
        }
        void ReleaseTexture(Rml::TextureHandle h) override {
            call([&] { target.ReleaseTexture(h); });
        }
        void EnableScissorRegion(bool b) override {
            if (!preparing && !blocked())
                call([&] { target.EnableScissorRegion(b); });
        }
        void SetScissorRegion(Rml::Rectanglei r) override {
            if (!preparing && !blocked())
                call([&] { target.SetScissorRegion(r); });
        }
        void SetTransform(const Rml::Matrix4f* m) override {
            if (!preparing && !blocked())
                call([&] { target.SetTransform(m); });
        }
        void EnableClipMask(bool b) override {
            if (!preparing && !blocked())
                call([&] { target.EnableClipMask(b); });
        }
        void RenderToClipMask(Rml::ClipMaskOperation o, Rml::CompiledGeometryHandle h,
                              Rml::Vector2f p) override {
            if (!preparing && !blocked())
                call([&] { target.RenderToClipMask(o, h, p); });
        }
        Rml::LayerHandle PushLayer() override {
            Rml::LayerHandle handle = 0;
            bool valid = !blocked() && layers.size() < ui_filter_detail::max_layers;
            if (!valid)
                fail("UI filter layer stack exceeds four active layers");
            else if (preparing)
                handle = virtual_layer++;
            else
                handle = call([&] { return target.PushLayer(); });
            valid &= handle != 0;
            if (!valid)
                fail("UI filter layer could not be allocated");
            layers.push_back({!preparing && valid, valid});
            return handle;
        }
        void PopLayer() override {
            if (layers.empty()) {
                fail("UI filter layer stack is unbalanced");
                return;
            }
            if (layers.back().forwarded)
                call([&] { target.PopLayer(); });
            layers.pop_back();
        }
        void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                             Rml::BlendMode blend,
                             Rml::Span<const Rml::CompiledFilterHandle> filters) override {
            if (filters.size() > ui_filter_detail::max_chain ||
                (blend != Rml::BlendMode::Blend && blend != Rml::BlendMode::Replace)) {
                fail("Invalid UI filter composition or chain exceeds eight");
                return;
            }
            if (!preparing && !blocked())
                call([&] { target.CompositeLayers(source, destination, blend, filters); });
        }
        Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                                const Rml::Dictionary& parameters) override {
            const auto handle = call([&] { return target.CompileFilter(name, parameters); });
            if (!handle)
                fail("UI filter could not be admitted by this renderer");
            return handle;
        }
        void ReleaseFilter(Rml::CompiledFilterHandle filter) override {
            call([&] { target.ReleaseFilter(filter); });
        }
        Rml::TextureHandle SaveLayerAsTexture() override {
            // At the exact native pin, a successful callback with handle zero
            // remains uncaptured and retries on the next Render. Never cache a
            // blank placeholder or read a framebuffer during preparation.
            if (preparing)
                return 0;
            const auto handle = call([&] { return target.SaveLayerAsTexture(); });
            if (!handle)
                fail("UI layer texture snapshot could not be captured");
            return handle;
        }
        Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
            // The pinned native producer keeps this filter local to one Render.
            // Preparation validates its count/lifetime through the existing
            // filter owner; it must never read or draw a host framebuffer.
            const auto handle = preparing ? call([&] {
                return target.CompileFilter("opacity", {{"value", Rml::Variant(1.f)}});
            })
                                          : call([&] { return target.SaveLayerAsMaskImage(); });
            if (!handle)
                fail("UI alpha mask could not be admitted by this renderer");
            return handle;
        }
        Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
                                                const Rml::Dictionary& parameters) override {
            const auto handle = call([&] { return target.CompileShader(name, parameters); });
            if (!handle)
                fail("UI gradient could not be admitted by this renderer");
            return handle;
        }
        void ReleaseShader(Rml::CompiledShaderHandle shader) override {
            call([&] { target.ReleaseShader(shader); });
        }
        void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation, Rml::TextureHandle texture) override {
            if (!preparing && !blocked())
                call([&] { target.RenderShader(shader, geometry, translation, texture); });
        }
    };
    std::string session, error, loading_prefix, failed_structure;
    LoadingState loading;
    std::string frame_statistics_label;
    std::optional<std::uint64_t> cancel_loading;
    Json loading_values{{"forge_loading_state", "idle"},    {"forge_loading_stage", ""},
                        {"forge_loading_completed", 0u},    {"forge_loading_total", 0u},
                        {"forge_loading_error_code", ""},   {"forge_loading_error", ""},
                        {"forge_loading_can_cancel", false}};
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
    std::unique_ptr<Prepared> staged;
    std::uint64_t preparation_serial = 0;
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
    std::unique_ptr<Rml::DecoratorInstancer> luminance_instancer;
    std::unique_ptr<Rml::ElementInstancer> shadow_instancer;
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
            luminance_instancer = register_ui_luminance_mask();
            shadow_instancer = register_ui_shadow_motion();
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
        if (staged)
            retire(staged->set);
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
            if (!loading_prefix.empty() &&
                (path.ends_with(".rcss") || path.ends_with(".ttf") || path.ends_with(".otf")))
                resources.at(path.substr(0, slash))->automatic(path.substr(slash + 1));
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
        for (const auto& [output, context] : set->outputs) {
            (void)output;
            if (context)
                Rml::RemoveContext(context->GetName());
        }
        Rml::RemoveContext(set->name);
        // Exact RmlUi6.3 ReleaseRenderManagers also releases ALL font resources
        // and updates surviving contexts. Do not invalidate another scene's
        // prepared glyphs or dispatch its events while retiring this context.
        if (Rml::GetNumContexts() == 0)
            Rml::ReleaseRenderManagers();
        resources.erase(set->name);
        set.reset();
    }
    bool enqueue_command(Doc& doc, const std::string& name,
                         const std::optional<std::string>& value) {
        if (!live || pending.size() >= 128 ||
            std::none_of(live->docs.begin(), live->docs.end(),
                         [&](const auto& d) { return d.get() == &doc; }))
            return false;
        const auto& allowed = doc.descriptor.at("commands");
        if (std::find(allowed.begin(), allowed.end(), Json(name)) == allowed.end()) {
            error = "UI command is not registered: " + name;
            return false;
        }
        if (value) {
            try {
                ui_protocol::validate_value(*value);
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
        }
        Json request = {{"version", current.at("version")}, {"session", session},
                        {"generation", generation},         {"id", ++command_id},
                        {"instance", doc.instance},         {"command", name}};
        if (value)
            request["value"] = *value;
        pending.push_back(std::move(request));
        return true;
    }
    void enqueue(Doc& doc, const Rml::VariantList& args) {
        if (args.empty() || args.size() > 2)
            return;
        std::optional<std::string> value;
        if (args.size() == 2)
            value = args[1].Get<Rml::String>();
        (void)enqueue_command(doc, args[0].Get<Rml::String>(), value);
    }
    void update_frame_statistics(Set& set) {
        if (frame_statistics_label.empty()) {
            if (set.frame_document)
                set.frame_document->Hide();
            return;
        }
        if (!set.frame_document) {
            static constexpr std::string_view markup = R"(<rml><head><style>
body, div { display:block; font-family:Lato; font-size:14dp; color:#ffffff; pointer-events:none; }
body { width:100%; height:100%; z-index:65536; }
#forge-frame-statistics { position:absolute; left:8dp; top:8dp; box-sizing:border-box; width:340dp; max-width:90%; padding:6dp; background-color:#101722ee; pointer-events:none; }
@media (max-height:160dp) { body, div { font-size:7vh; } }
</style></head><body><div id="forge-frame-statistics"></div></body></rml>)";
            validate_ui_text(markup, true);
            auto* document = set.context->LoadDocumentFromMemory(
                std::string(markup), set.name + "/__forge_frame_statistics.rml");
            if (!document)
                throw std::runtime_error("Host frame statistics document could not be loaded");
            try {
                auto* panel = document->GetElementById("forge-frame-statistics");
                if (!panel)
                    throw std::runtime_error("Host frame statistics text panel is unavailable");
                auto text = document->CreateTextNode(frame_statistics_label);
                if (!text)
                    throw std::runtime_error("Host frame statistics text could not be created");
                // CreateTextNode guarantees an ElementText; keep text literal, never RML.
                auto* value = static_cast<Rml::ElementText*>(text.get());
                panel->AppendChild(std::move(text));
                set.frame_text = value;
                set.frame_document = document;
            } catch (...) {
                document->Close();
                throw;
            }
        } else
            set.frame_text->SetText(frame_statistics_label);
        set.frame_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        set.frame_document->PullToFront();
    }
    std::unique_ptr<Set> build(const Json& state, bool static_inspection = false) {
        auto candidate = std::make_unique<Set>();
        candidate->name = "ui" + std::to_string(++serial);
        candidate->resources = std::make_shared<UiResources>(project);
        candidate->localization = state.value("localization", Json());
        candidate->application_states = state.value("application_states", Json());
        resources[candidate->name] = candidate->resources;
        error.clear();
        loading_prefix = candidate->name;
        try {
            if (!candidate->localization.is_null())
                candidate->messages = &candidate->resources->localization(
                    {candidate->localization.at("asset").get<AssetId>()});
            Rml::Factory::ClearStyleSheetCache();
            Rml::Factory::ClearTemplateCache();
            candidate->context = Rml::CreateContext(candidate->name, {width, height}, &renderer);
            if (!candidate->context)
                throw std::runtime_error("UI context creation failed");
            candidate->context->SetDensityIndependentPixelRatio(density);
            auto descriptors = state.at("documents").get<std::vector<Json>>();
            std::stable_sort(
                descriptors.begin(), descriptors.end(),
                [](const auto& a, const auto& b) { return a.at("layer") < b.at("layer"); });
            std::set<Rml::Context*> initialized_contexts;
            for (const auto& desc : descriptors) {
                auto d = std::make_unique<Doc>();
                d->instance = desc.at("instance");
                d->descriptor = desc;
                d->context = candidate->context;
                const auto output_value = desc.value("render_texture", Json());
                if (!output_value.is_null()) {
                    const auto output = output_value.get<AssetId>();
                    auto [at, added] = candidate->outputs.try_emplace(output, nullptr);
                    if (added) {
                        at->second = Rml::CreateContext(candidate->name + "-" + output.str(),
                                                        {width, height}, &renderer);
                        if (!at->second)
                            throw std::runtime_error("UI output context creation failed");
                        at->second->SetDensityIndependentPixelRatio(1);
                    }
                    d->context = at->second;
                }
                d->model_name = "forge" + std::to_string(candidate->docs.size());
                auto model = d->context->CreateDataModel(d->model_name);
                if (!model)
                    throw std::runtime_error("UI data model creation failed");
                auto* ptr = d.get();
                const bool first_model = initialized_contexts.insert(d->context).second;
                if (!candidate->application_states.is_null()) {
                    for (const auto& [key, value] : desc.at("model").items()) {
                        (void)value;
                        if (key.starts_with("forge_state_") || key.starts_with("forge_rev_"))
                            throw std::runtime_error(
                                "Application state UI reserves its state presentation bindings");
                    }
                    if (first_model)
                        model.RegisterTransformFunc(
                            "state_is", [set = candidate.get()](const Rml::VariantList& args) {
                                if (args.size() != 3 || args[0].GetType() != Rml::Variant::STRING ||
                                    args[1].GetType() != Rml::Variant::STRING ||
                                    args[2].GetType() != Rml::Variant::STRING)
                                    return Rml::Variant(false);
                                const auto domain = args[1].Get<Rml::String>();
                                const auto member = args[2].Get<Rml::String>();
                                for (const auto& row : set->application_states.at("domains"))
                                    if (row.at("key") == domain &&
                                        row.at("active") == args[0].Get<Rml::String>()) {
                                        const auto& membership = row.at("membership");
                                        return Rml::Variant(std::find(membership.begin(),
                                                                      membership.end(),
                                                                      member) != membership.end());
                                    }
                                return Rml::Variant(false);
                            });
                    for (std::size_t index = 0;
                         index < candidate->application_states.at("domains").size(); ++index) {
                        const auto domain = candidate->application_states.at("domains")[index]
                                                .at("key")
                                                .get<std::string>();
                        for (bool revision : {false, true}) {
                            const auto name =
                                std::string(revision ? "forge_rev_" : "forge_state_") + domain;
                            if (!model.BindFunc(name, [set = candidate.get(), index,
                                                       revision](Rml::Variant& out) {
                                    const auto& row = set->application_states.at("domains")[index];
                                    out = revision ? std::to_string(
                                                         row.at("revision").get<std::uint64_t>())
                                                   : row.at("active").get<std::string>();
                                }))
                                throw std::runtime_error("UI application state binding failed");
                        }
                    }
                }
                if (candidate->messages) {
                    if (first_model)
                        model.RegisterTransformFunc(
                            "tr", [table = candidate->messages](const Rml::VariantList& args) {
                                if (args.size() != 2 || args[0].GetType() != Rml::Variant::STRING ||
                                    args[1].GetType() != Rml::Variant::STRING)
                                    return Rml::Variant("[[invalid translation]]");
                                const auto locale = args[1].Get<Rml::String>();
                                if (!ui_locale_tag(locale))
                                    return Rml::Variant("[[invalid locale]]");
                                return Rml::Variant(
                                    table->message(args[0].Get<Rml::String>(), locale).text);
                            });
                    for (const auto* key : {"forge_locale", "forge_resolved_locale"}) {
                        if (desc.at("model").contains(key))
                            throw std::runtime_error("Localized UI reserves " + std::string(key));
                        const bool resolved = std::string_view(key) == "forge_resolved_locale";
                        if (!model.BindFunc(
                                key, [set = candidate.get(), resolved](Rml::Variant& out) {
                                    const auto locale =
                                        set->localization.at("locale").get<std::string>();
                                    out = resolved ? set->messages->select_locale(locale) : locale;
                                }))
                            throw std::runtime_error("UI locale binding failed");
                    }
                }
                for (const auto& [key, value] : loading_values.items()) {
                    (void)value;
                    if (desc.at("model").contains(key))
                        throw std::runtime_error("UI model uses reserved host value: " + key);
                    if (!model.BindFunc(key, [this, key](Rml::Variant& out) {
                            const auto& value = loading_values.at(key);
                            if (value.is_boolean())
                                out = value.get<bool>();
                            else if (value.is_number())
                                out = value.get<std::uint64_t>();
                            else
                                out = value.get<std::string>();
                        }))
                        throw std::runtime_error("UI host loading binding failed");
                }
                model.BindEventCallback("cancel_loading", [this,
                                                           ptr](Rml::DataModelHandle, Rml::Event&,
                                                                const Rml::VariantList& args) {
                    if (args.empty() && loading.can_cancel && live &&
                        std::any_of(live->docs.begin(), live->docs.end(), [ptr](const auto& doc) {
                            return doc.get() == ptr &&
                                   doc->descriptor.at("visible").template get<bool>();
                        }))
                        cancel_loading = loading.ticket;
                });
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
                candidate->resources->automatic(source);
                const auto& bytes = candidate->resources->read(source);
                std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (text.find("data-model") != std::string::npos)
                    throw std::runtime_error(
                        "FORGE supplies the document data model; omit data-model attributes");
                auto body = text.find("<body");
                if (body == std::string::npos)
                    throw std::runtime_error("UI document needs body");
                if (!static_inspection)
                    text.insert(body + 5, " data-model=\"" + d->model_name + "\"");
                d->document =
                    d->context->LoadDocumentFromMemory(text, candidate->name + "/" + source);
                if (!d->document)
                    throw std::runtime_error("UI document load failed");
                // Native parsed DOM, not a second RML parser. Only literal image
                // attributes are automatic; state/data-selected URLs are declared.
                std::vector<Rml::Element*> elements{d->document};
                std::size_t visited = 0;
                while (!elements.empty()) {
                    auto* element = elements.back();
                    elements.pop_back();
                    if (++visited > 8192)
                        throw std::runtime_error(
                            "UI automatic resource inspection exceeds node limit");
                    if (element->GetTagName() == "img" && element->HasAttribute("src") &&
                        !element->HasAttribute("data-attr-src") &&
                        element->GetAttribute<Rml::String>("sprite", "").empty()) {
                        const auto image = element->GetAttribute<Rml::String>("src", "");
                        candidate->resources->automatic(candidate->resources->join(source, image));
                    }
                    for (int i = 0; i < element->GetNumChildren(); ++i)
                        elements.push_back(element->GetChild(i));
                }
                d->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
                candidate->docs.push_back(std::move(d));
            }
            if (!static_inspection)
                update_frame_statistics(*candidate);
            candidate->context->Update();
            synchronize_ui_shadow_motion(*candidate->context);
            for (const auto& [output, context] : candidate->outputs) {
                (void)output;
                context->Update();
                synchronize_ui_shadow_motion(*context);
            }
            if (!error.empty())
                throw std::runtime_error(error);
            renderer.preparing = true;
            renderer.render(*candidate->context);
            for (const auto& [output, context] : candidate->outputs) {
                (void)output;
                renderer.render(*context);
            }
            renderer.preparing = false;
            for (auto& d : candidate->docs)
                if (!d->descriptor.at("visible").get<bool>())
                    d->document->Hide();
            if (!error.empty())
                throw std::runtime_error(error);
            candidate->assets = candidate->resources->snapshot();
            loading_prefix.clear();
            return candidate;
        } catch (const std::exception& e) {
            renderer.preparing = false;
            loading_prefix.clear();
            const std::string failure = e.what();
            retire(candidate);
            error = failure;
            return {};
        }
    }
    bool rebuild() {
        auto next = build(current);
        if (!next)
            return false;
        held_keys.clear();
        auto previous = std::move(live);
        retire(previous);
        live = std::move(next);
        prepared_outputs.clear();
        ++presentation_revision;
        return true;
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
    if (impl_->live && impl_->error.empty() &&
        impl_->live->assets.sources.size() != impl_->live->resources->source_count())
        impl_->live->assets = impl_->live->resources->snapshot();
    return impl_->live ? &impl_->live->assets : nullptr;
}
UiAssetSnapshot UiPresenter::inspect_static(AssetRef<UiDocumentAsset> asset) {
    auto& s = *impl_;
    s.check();
    if (s.live || s.staged)
        throw std::runtime_error("Static UI inspection requires an isolated empty presenter");
    const auto entity = EntityId::generate();
    Json state{{"documents", Json::array({{{"entity", entity},
                                           {"asset", asset.id},
                                           {"instance", entity.str()},
                                           {"visible", false},
                                           {"layer", 0u},
                                           {"model", Json::object()},
                                           {"commands", Json::array()}}})}};
    auto candidate = s.build(state, true);
    if (!candidate)
        throw std::runtime_error(s.error);
    auto result = candidate->assets;
    s.retire(candidate);
    return result;
}
void UiPresenter::reset(std::string session, std::uint64_t generation) {
    auto& s = *impl_;
    s.check();
    if (s.staged)
        s.retire(s.staged->set);
    s.staged.reset();
    s.retire(s.live);
    s.held_keys.clear();
    s.pending.clear();
    s.cancel_loading.reset();
    s.command_id = 0;
    s.session = std::move(session);
    s.generation = generation;
    s.current = nullptr;
    s.prepared_outputs.clear();
    s.error.clear();
    s.failed_structure.clear();
    s.replica.reset(s.session, generation);
}
std::uint64_t UiPresenter::prepare(const Json& snapshot) {
    auto& s = *impl_;
    s.check();
    if (s.staged)
        s.retire(s.staged->set);
    s.staged.reset();
    if (s.preparation_serial == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("UI preparation generation exhausted");
    auto next = std::make_unique<Impl::Prepared>();
    next->ticket = ++s.preparation_serial;
    ui_protocol::validate_snapshot(snapshot);
    next->session = snapshot.at("session");
    next->generation = snapshot.at("generation");
    next->replica.reset(next->session, next->generation);
    if (!next->replica.accept(snapshot))
        throw std::runtime_error("UI preparation snapshot was not admitted");
    next->state = snapshot;
    next->set = s.build(snapshot);
    if (!next->set)
        throw std::runtime_error(s.error);
    const auto ticket = next->ticket;
    s.staged = std::move(next);
    return ticket;
}
bool UiPresenter::prepared(std::uint64_t ticket) const {
    const auto& s = *impl_;
    s.check();
    return s.staged && s.staged->ticket == ticket;
}
bool UiPresenter::activate_prepared(std::uint64_t ticket) noexcept {
    auto& s = *impl_;
    if (std::this_thread::get_id() != s.thread || !s.staged || s.staged->ticket != ticket)
        return false;
    auto previous = std::move(s.live);
    s.retire(previous);
    s.live = std::move(s.staged->set);
    s.prepared_outputs.clear();
    s.current = std::move(s.staged->state);
    s.replica = std::move(s.staged->replica);
    s.session = std::move(s.staged->session);
    s.generation = s.staged->generation;
    s.cancel_loading.reset();
    s.staged.reset();
    s.held_keys.clear();
    s.pending.clear();
    s.command_id = 0;
    s.failed_structure.clear();
    s.error.clear();
    ++s.presentation_revision;
    return true;
}
void UiPresenter::cancel_prepared(std::uint64_t ticket) {
    auto& s = *impl_;
    s.check();
    if (!s.staged || s.staged->ticket != ticket)
        throw std::runtime_error("Stale or missing prepared UI");
    s.retire(s.staged->set);
    s.staged.reset();
}
bool UiPresenter::accept(const Json& j) {
    auto& s = *impl_;
    s.check();
    if (!s.replica.accept(j))
        return false;
    bool rebuild = !s.live;
    const auto localization = j.value("localization", Json());
    const auto application_states = j.value("application_states", Json());
    const auto& docs = j.at("documents");
    if (s.live) {
        rebuild =
            s.live->docs.size() != docs.size() ||
            localization_identity(localization) != localization_identity(s.live->localization) ||
            application_state_identity(application_states) !=
                application_state_identity(s.live->application_states);
        for (const auto& d : s.live->docs) {
            auto it = std::find_if(docs.begin(), docs.end(),
                                   [&](const auto& v) { return v.at("instance") == d->instance; });
            if (it == docs.end()) {
                rebuild = true;
                break;
            }
            if (it->value("render_texture", Json()) !=
                    d->descriptor.value("render_texture", Json()) ||
                it->at("layer") != d->descriptor.at("layer") ||
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
        const auto signature =
            Json{{"documents", structure},
                 {"localization", localization_identity(localization)},
                 {"application_states", application_state_identity(application_states)}}
                .dump();
        if (signature == s.failed_structure)
            return false;
        const bool ok = s.rebuild();
        s.failed_structure = ok ? std::string{} : signature;
        return ok;
    }
    bool visibility_changed = false;
    const bool locale_changed = s.live->localization != localization;
    const bool states_changed = s.live->application_states != application_states;
    for (const auto& d : s.live->docs)
        for (const auto& desc : docs)
            if (desc.at("instance") == d->instance &&
                desc.at("visible") != d->descriptor.at("visible"))
                visibility_changed = true;
    if (visibility_changed)
        release_input();
    if (visibility_changed || locale_changed || states_changed) {
        ++s.presentation_revision;
    }
    s.live->localization = localization;
    s.live->application_states = application_states;
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
void UiPresenter::loading(const LoadingState& value) {
    auto& s = *impl_;
    s.check();
    static const std::set<std::string> states{"idle",      "preparing", "loading", "ready",
                                              "activated", "cancelled", "failed",  "faulted"};
    if (!states.contains(value.state) || value.stage.size() > 256 || value.error.size() > 1024 ||
        value.error_code.size() > 64 || value.completed > value.total ||
        (value.can_cancel && !value.ticket))
        throw std::runtime_error("Invalid host loading state");
    Json next{{"forge_loading_state", value.state},           {"forge_loading_stage", value.stage},
              {"forge_loading_completed", value.completed},   {"forge_loading_total", value.total},
              {"forge_loading_error_code", value.error_code}, {"forge_loading_error", value.error},
              {"forge_loading_can_cancel", value.can_cancel}};
    if (s.loading.ticket != value.ticket || !value.can_cancel)
        s.cancel_loading.reset();
    s.loading = value;
    if (next == s.loading_values)
        return;
    s.loading_values = std::move(next);
    auto dirty = [](auto& set) {
        if (set)
            for (auto& doc : set->docs)
                doc->model.DirtyAllVariables();
    };
    dirty(s.live);
    if (s.staged)
        dirty(s.staged->set);
}
std::optional<std::uint64_t> UiPresenter::take_loading_cancel() {
    impl_->check();
    return std::exchange(impl_->cancel_loading, std::nullopt);
}
void UiPresenter::update(double t, int w, int h, float dp) {
    auto& s = *impl_;
    s.check();
    if (!std::isfinite(t) || t < s.time || w < 1 || h < 1 || w > 8192 || h > 8192 ||
        !std::isfinite(dp) || dp < .5f || dp > 4)
        throw std::runtime_error("Invalid UI presentation dimensions/time");
    s.prepared_outputs.clear();
    s.time = t;
    s.width = w;
    s.height = h;
    s.density = dp;
    if (s.live) {
        s.live->context->SetDimensions({w, h});
        s.live->context->SetDensityIndependentPixelRatio(dp);
        s.renderer.checked(
            [&] {
                s.live->context->Update();
                synchronize_ui_shadow_motion(*s.live->context);
            },
            false);
    }
}
void UiPresenter::update_output(AssetRef<RenderTextureAsset> output, int w, int h, float dp) {
    auto& s = *impl_;
    s.check();
    if (!output.id || w < 1 || h < 1 || w > 8192 || h > 8192 || !std::isfinite(dp) || dp < .5f ||
        dp > 4)
        throw std::runtime_error("Invalid UI output viewport");
    if (!s.live)
        return;
    const auto at = s.live->outputs.find(output.id);
    if (at == s.live->outputs.end())
        return;
    auto* context = at->second;
    const bool resized = context->GetDimensions() != Rml::Vector2i{w, h} ||
                         context->GetDensityIndependentPixelRatio() != dp;
    context->SetDimensions({w, h});
    context->SetDensityIndependentPixelRatio(dp);
    if (s.prepared_outputs.insert(output.id).second || resized)
        s.renderer.checked([&] {
            context->Update();
            synchronize_ui_shadow_motion(*context);
        });
}

void UiPresenter::frame_statistics(const std::string& text) {
    auto& s = *impl_;
    s.check();
    if (text.size() > 128)
        throw std::runtime_error("Host frame statistics text exceeds 128 bytes");
    ui_protocol::validate_value(text);
    if (text == s.frame_statistics_label)
        return;
    s.frame_statistics_label = text;
    if (s.live)
        s.update_frame_statistics(*s.live);
    if (s.staged)
        s.update_frame_statistics(*s.staged->set);
}
std::vector<AssetId> UiPresenter::output_targets() const {
    auto& s = *impl_;
    s.check();
    std::vector<AssetId> result;
    if (s.live)
        for (const auto& [output, context] : s.live->outputs) {
            (void)context;
            result.push_back(output);
        }
    return result;
}
bool UiPresenter::has_output(AssetRef<RenderTextureAsset> output) const {
    auto& s = *impl_;
    s.check();
    return output.id && s.live && s.live->outputs.contains(output.id);
}
void UiPresenter::render_output(AssetRef<RenderTextureAsset> output) {
    auto& s = *impl_;
    s.check();
    if (!output.id) {
        render();
        return;
    }
    if (!s.live)
        return;
    const auto at = s.live->outputs.find(output.id);
    if (at == s.live->outputs.end())
        return;
    if (!s.prepared_outputs.contains(output.id))
        throw std::runtime_error("UI output viewport has not been prepared");
    s.renderer.render(*at->second);
}
void UiPresenter::render() {
    auto& s = *impl_;
    s.check();
    if (s.live) {
        if (!s.frame_statistics_label.empty() && s.live->frame_document)
            s.live->frame_document->PullToFront();
        s.renderer.render(*s.live->context, false);
    }
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
    // Documents are shown without stealing pointer/text focus. The first Tab
    // (including a controller next/previous action) deliberately enters the
    // highest visible document, then native RmlUi owns control traversal.
    auto* focused = s.live->context->GetFocusElement();
    if (down && k == Rml::Input::KI_TAB && (!focused || !focused->GetOwnerDocument())) {
        for (auto it = s.live->docs.rbegin(); it != s.live->docs.rend(); ++it)
            if ((*it)->document->IsVisible()) {
                (*it)->document->Focus();
                break;
            }
    }
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
void UiPresenter::navigate(const std::string& direction) {
    if (direction != "next" && direction != "previous" && direction != "accept")
        throw std::runtime_error("Unknown UI navigation action");
    const auto key_code = direction == "accept" ? Rml::Input::KI_RETURN : Rml::Input::KI_TAB;
    const int modifiers = direction == "previous" ? Rml::Input::KM_SHIFT : 0;
    key(key_code, true, modifiers);
    key(key_code, false, modifiers);
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
bool UiPresenter::command(const std::string& instance, const std::string& name,
                          const std::optional<std::string>& value) {
    auto& s = *impl_;
    s.check();
    if (s.live)
        for (auto& doc : s.live->docs)
            if (doc->instance == instance)
                return s.enqueue_command(*doc, name, value);
    return false;
}
std::optional<Json> UiPresenter::pending_command() const {
    auto& s = *impl_;
    s.check();
    return s.pending.empty() ? std::nullopt : std::optional<Json>(s.pending.front());
}
void UiPresenter::acknowledge(const Json& ack) {
    auto& s = *impl_;
    s.check();
    if (s.pending.empty() ||
        ack.value("version", 0u) != s.pending.front().at("version").get<unsigned>() ||
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
Json UiPresenter::localization_state() const {
    const auto& s = *impl_;
    s.check();
    if (!s.live || !s.live->messages)
        return Json();
    const auto& table = *s.live->messages;
    const auto requested = s.live->localization.at("locale").get<std::string>();
    const auto selected = table.select_locale(requested);
    const auto total = table.locales.at(table.default_locale).messages.size();
    return {{"entity", s.live->localization.at("entity")},
            {"asset", table.asset},
            {"requested_locale", requested},
            {"selected_locale", selected},
            {"locale_fallback", canonical_ui_locale(requested) != selected},
            {"default_locale", table.default_locale},
            {"messages", total},
            {"fallback_messages", total - table.locales.at(selected).messages.size()}};
}
Json UiPresenter::application_states_state() const {
    const auto& s = *impl_;
    s.check();
    return s.live ? s.live->application_states : Json();
}
} // namespace forge
