#include <RmlUi/Core.h>
#include <forge/ui_presenter.hpp>
#include <fstream>
#include <iostream>
#include <set>
using namespace forge;
using Json = nlohmann::json;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Renderer final : Rml::RenderInterface {
    std::set<std::uintptr_t> geometry, textures;
    std::uintptr_t next = 1;
    unsigned draws = 0, clips = 0, transforms = 0;
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                Rml::Span<const int> i) override {
        check(!v.empty() && !i.empty(), "Geometry data");
        auto id = next++;
        geometry.insert(id);
        return id;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle g, Rml::Vector2f,
                        Rml::TextureHandle t) override {
        check(geometry.contains(g), "Live geometry");
        check(!t || textures.contains(t), "Live texture");
        ++draws;
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle g) override {
        check(geometry.erase(g) == 1, "Geometry retired once");
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String& path) override {
        Rml::String bytes;
        if (!Rml::GetFileInterface()->LoadFile(path, bytes))
            return 0;
        auto image =
            decode_ui_image({reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
        dims = {int(image.width), int(image.height)};
        return GenerateTexture(
            {reinterpret_cast<const Rml::byte*>(image.rgba.data()), image.rgba.size()}, dims);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> bytes,
                                       Rml::Vector2i dims) override {
        check(bytes.size() == std::size_t(dims.x) * dims.y * 4, "Texture dimensions");
        auto id = next++;
        textures.insert(id);
        return id;
    }
    void ReleaseTexture(Rml::TextureHandle t) override {
        check(textures.erase(t) == 1, "Texture retired once");
    }
    void EnableScissorRegion(bool) override { ++clips; }
    void SetScissorRegion(Rml::Rectanglei) override { ++clips; }
    void SetTransform(const Rml::Matrix4f*) override { ++transforms; }
    void EnableClipMask(bool) override {}
    void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle,
                          Rml::Vector2f) override {}
};
std::vector<std::byte> read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::string b{std::istreambuf_iterator<char>(f), {}};
    std::vector<std::byte> v(b.size());
    std::memcpy(v.data(), b.data(), b.size());
    return v;
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need root and font");
        auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const std::string rml =
            R"rml(<rml><head><style>body {font-family:Lato;font-size:18px;} button {display:block;width:180px;height:40px;background-color:#305070;} #field {display:block;width:180px;height:30px;} #box {width:150px;height:30px;overflow:hidden;transform:translateX(10px);}</style></head><body><button id="pause" data-event-click="command('Pause')">Pause {{tick}}</button><input id="field" type="text"/><div id="box">Bound value {{tick}} paused {{paused}}</div></body></rml>)rml";
        std::ofstream(root / "hud.rml") << rml;
        auto asset = register_ui_document(root, "hud.rml");
        auto entity = EntityId::generate();
        Json state = {{"version", 1},
                      {"session", "test"},
                      {"generation", 1u},
                      {"revision", 1u},
                      {"documents", Json::array({{{"entity", entity},
                                                  {"asset", asset.id},
                                                  {"instance", entity.str() + ":" + asset.id.str()},
                                                  {"visible", true},
                                                  {"layer", 0u},
                                                  {"model", {{"tick", 2u}, {"paused", true}}},
                                                  {"commands", {"Pause", "Resume"}}}})}};
        Renderer renderer;
        {
            UiPresenter p(renderer, root, read(argv[2]));
            p.reset("test", 1);
            check(p.accept(state), p.diagnostic().c_str());
            p.update(1, 800, 600);
            p.render();
            check(renderer.draws > 0 && !renderer.textures.empty(), "Text and geometry render");
            check(p.document_count() == 1, "One document loaded");
            auto* context = Rml::GetContext(0);
            auto* button = context->GetDocument(0)->GetElementById("pause");
            check(button, "Button loaded");
            auto position = button->GetAbsoluteOffset();
            check(p.mouse_move(int(position.x + 15), int(position.y + 15), 0), "Mouse UI capture");
            p.mouse_button(0, true, 0);
            p.mouse_button(0, false, 0);
            p.update(2, 800, 600);
            auto command = p.pending_command();
            check(command && command->at("command") == "Pause", "Semantic event emitted");
            check(command->at("session") == "test" && command->at("generation") == 1,
                  "Correlated command");
            auto ack = *command;
            ack["ok"] = true;
            p.acknowledge(ack);
            check(!p.pending_command(), "Acknowledgement retires event");
            auto* field = context->GetDocument(0)->GetElementById("field");
            position = field->GetAbsoluteOffset();
            p.mouse_move(int(position.x + 12), int(position.y + 12), 0);
            p.mouse_button(0, true, 0);
            p.mouse_button(0, false, 0);
            check(p.wants_text(), "Text focus");
            check(p.key(Rml::Input::KI_W, true, 0), "Text input captures gameplay key");
            p.text("W");
            p.key(Rml::Input::KI_W, false, 0);
            p.release_input();
            check(!p.wants_text(), "Focus release");
            state["revision"] = 2u;
            state["documents"][0]["model"]["tick"] = 3u;
            check(p.accept(state), "Copied model updates while paused");
            p.update(3, 420, 320, 1.5f);
            p.render();
            check(context->GetDimensions() == Rml::Vector2i(420, 320), "Context resized");
            auto stale = state;
            stale["generation"] = 2u;
            stale["revision"] = 3u;
            check(!p.accept(stale), "Reject foreign generation");
            std::ofstream(root / "hud.rml") << "<rml><body><div></body>";
            check(!p.reload(), "Malformed replacement rejected");
            check(p.document_count() == 1, "Previous document retained");
            p.update(4, 420, 320);
            p.render();
            std::ofstream(root / "hud.rml") << "<rml><body><img src=\"absent.tga\" /></body></rml>";
            check(!p.reload() && p.document_count() == 1,
                  "Missing image preserves previous document");
            auto publication = p.presentation_revision();
            std::ofstream(root / "hud.rml") << rml;
            check(p.reload(), "Valid replacement publishes");
            check(p.presentation_revision() > publication,
                  "Same-count replacement has new revision");
            p.update(5, 640, 480);
            p.render();
            state["revision"] = 3u;
            state["documents"][0]["visible"] = false;
            check(p.accept(state), "Visibility update");
            p.update(6, 640, 480);
            auto count = renderer.draws;
            p.render();
            check(renderer.draws == count, "Hidden document not rendered");
            // Linked resources, scalar strings and deterministic multi-document ordering.
            std::ofstream(root / "hud.rml")
                << "<rml><head><link type=\"text/rcss\" "
                   "href=\"missing.rcss\"/></head><body>Missing style</body></rml>";
            check(!p.reload() && p.document_count() == 1,
                  "Missing stylesheet retains last usable document");
            std::filesystem::copy_file(argv[2], root / "project-font.ttf");
            std::ofstream(root / "hud.rcss")
                << "@font-face {font-family:ProjectTest;src:\"project-font.ttf\";} body "
                   "{font-family:ProjectTest;font-size:18px;}";
            std::ofstream(root / "hud.rml")
                << "<rml><head><link type=\"text/rcss\" href=\"hud.rcss\"/></head><body><p "
                   "id=\"name\">{{name}}</p></body></rml>";
            state["documents"][0]["visible"] = true;
            state["documents"][0]["layer"] = 5u;
            state["documents"][0]["model"]["name"] = "First";
            auto overlay = state["documents"][0];
            overlay["entity"] = EntityId::generate();
            overlay["instance"] = overlay["entity"].get<std::string>() + ":1";
            overlay["layer"] = 1u;
            overlay["model"]["name"] = "Underneath";
            state["documents"].push_back(overlay);
            state["revision"] = 4u;
            if (!p.accept(state))
                throw std::runtime_error("Multiple UI documents: " + p.diagnostic());
            check(p.document_count() == 2, "Multiple UI documents count");
            p.update(7, 640, 480);
            p.render();
            context = Rml::GetContext(0);
            check(context->GetDocument(0)->GetElementById("name")->GetInnerRML().find(
                      "Underneath") != std::string::npos,
                  "Layer ordering and string binding");
            state["documents"][1]["model"]["name"] = "Changed";
            state["revision"] = 5u;
            check(p.accept(state), "String model change");
            p.update(8, 640, 480);
            check(context->GetDocument(0)->GetElementById("name")->GetInnerRML().find("Changed") !=
                      std::string::npos,
                  "Latest copied string displayed");
            p.reset("recovered", 2);
            check(p.document_count() == 0 && !p.pending_command(),
                  "Recovery clears stale presentation/commands");
            state["session"] = "recovered";
            state["generation"] = 2u;
            state["revision"] = 1u;
            check(p.accept(state), "Recovery rebuilds admitted document");
        }
        {
            UiPresenter again(renderer, root, read(argv[2]));
            again.reset("second", 1);
        }
        check(renderer.geometry.empty() && renderer.textures.empty(),
              "RmlUi releases resources before renderer destruction");
        std::filesystem::remove_all(root);
        std::cout << "RmlUi presentation, input, binding, replacement and lifetime checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
