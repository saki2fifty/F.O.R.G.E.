#ifdef FORGE_UI_CATALOG_TEST
#include "ui_asset_catalog.hpp"
#endif
#include "ui_test_renderer.hpp"
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
using Renderer = UiTestRenderer;
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
            R"rml(<rml><head><style>body {font-family:Lato;font-size:18px;} button {display:block;width:180px;height:40px;background-color:#305070;} #field {display:block;width:180px;height:30px;} #box {width:150px;height:30px;overflow:hidden;transform:translateX(10px);}</style></head><body><button id="pause" data-event-click="command('Pause')">Pause {{tick}}</button><input id="field" type="text"/><div id="box">Bound value {{tick}} paused {{paused}}</div><div id="loading">{{forge_loading_state}} {{forge_loading_stage}} {{forge_loading_completed}}/{{forge_loading_total}} {{forge_loading_error}}</div><button id="cancel-loading" data-event-click="cancel_loading()">Cancel loading</button></body></rml>)rml";
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
            check(p.asset_snapshot() && p.asset_snapshot()->documents.contains(asset.id) &&
                      p.asset_snapshot()->sources.size() == 1,
                  "Successful presenter exposes admitted source observation");
            const auto initial_sources = p.asset_snapshot()->sources;
            auto* context = Rml::GetContext(0);
            LoadingState loading{7, 0, "loading", "graphics", {}, {}, 1, 4, true};
            p.loading(loading);
            p.update(1, 800, 600);
            auto* loading_label = context->GetDocument(0)->GetElementById("loading");
            check(loading_label->GetInnerRML().find("loading graphics 1/4") != std::string::npos,
                  "Copied loading progress did not reach native RmlUi binding");
            auto* cancel_button = context->GetDocument(0)->GetElementById("cancel-loading");
            cancel_button->DispatchEvent("click", Rml::Dictionary{});
            check(p.take_loading_cancel() == 7 && !p.take_loading_cancel(),
                  "Loading cancellation was not single-consumption/ticketed");
            cancel_button->DispatchEvent("click", Rml::Dictionary{});
            loading.ticket = 8;
            p.loading(loading);
            check(!p.take_loading_cancel(), "Superseded loading cancellation survived");
            loading.state = "failed";
            loading.can_cancel = false;
            loading.error = "Required mesh missing";
            p.loading(loading);
            p.update(1, 800, 600);
            check(loading_label->GetInnerRML().find("Required mesh missing") != std::string::npos,
                  "Loading error absent from RmlUi");
            cancel_button->DispatchEvent("click", Rml::Dictionary{});
            check(!p.take_loading_cancel(), "Terminal loading state accepted cancel");
            p.loading({});
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
            check(p.asset_snapshot() && p.asset_snapshot()->sources == initial_sources,
                  "Failed reload replaced the last-good resource observation");
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
#ifdef FORGE_UI_CATALOG_TEST
            {
                const auto* observed = p.asset_snapshot();
                check(
                    observed && observed->sources.size() == 3 && observed->documents.size() == 1,
                    "RmlUi stylesheet/font dependencies not captured or repeated root duplicated");
                ProjectLease writer(root);
                const auto catalog = refresh_ui_asset_catalog(writer, *observed);
                const auto& root_record = catalog.records().at(asset.id);
                check(root_record.dependency_edges.size() == 2 &&
                          root_record.source_dependencies.size() == 3,
                      "Native UI dependencies were not published through the common graph");
                const auto first = catalog.document();
                check(refresh_ui_asset_catalog(writer, *observed).document() == first,
                      "UI metadata refresh changed stable source identities");
                const auto old_style = read(root / "hud.rcss");
                std::ofstream(root / "hud.rcss", std::ios::app) << "\n/* external edit */";
                bool rejected = false;
                try {
                    refresh_ui_asset_catalog(writer, *observed);
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected && AssetCatalog::open_project(root).document() == first,
                      "Stale native UI observation modified the catalog");
                std::ofstream restored(root / "hud.rcss", std::ios::binary | std::ios::trunc);
                restored.write(reinterpret_cast<const char*>(old_style.data()), old_style.size());
            }
#endif
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
            check(!p.asset_snapshot(), "Reset kept a borrowed old UI observation");
            check(p.document_count() == 0 && !p.pending_command(),
                  "Recovery clears stale presentation/commands");
            state["session"] = "recovered";
            state["generation"] = 2u;
            state["revision"] = 1u;
            check(p.accept(state), "Recovery rebuilds admitted document");
            auto* previous = Rml::GetContext(0);
            const auto revision = p.presentation_revision();
            auto replacement = state;
            replacement["session"] = "transition";
            replacement["generation"] = 3u;
            replacement["documents"] = Json::array();
            const auto draw_count = renderer.draws;
            auto ticket = p.prepare(replacement);
            check(p.prepared(ticket) && Rml::GetNumContexts() == 2 && p.document_count() == 2 &&
                      p.presentation_revision() == revision && renderer.draws == draw_count,
                  "Preparing new scene changed live documents/framebuffer");
            p.update(9, 640, 480);
            p.render();
            check(Rml::GetContext(0) == previous && renderer.draws > draw_count,
                  "Old UI stopped rendering while candidate pending");
            p.cancel_prepared(ticket);
            check(!p.prepared(ticket) && Rml::GetNumContexts() == 1 && !p.activate_prepared(ticket),
                  "Cancelled UI remained publishable");
            ticket = p.prepare(replacement);
            auto malformed = replacement;
            malformed["documents"] = state["documents"];
            malformed["documents"][0]["asset"] = AssetId::generate();
            bool rejected = false;
            try {
                p.prepare(malformed);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && !p.prepared(ticket) && p.document_count() == 2 &&
                      Rml::GetNumContexts() == 1 && Rml::GetContext(0) == previous,
                  "Failed candidate damaged live UI or retained superseded candidate");
            ticket = p.prepare(replacement);
            check(p.activate_prepared(ticket) && p.document_count() == 0 &&
                      Rml::GetNumContexts() == 1 && p.presentation_revision() == revision + 1,
                  "Prepared UI publication failed");
            check(!p.activate_prepared(ticket) && !p.accept(state),
                  "Old generation or duplicate publication admitted");
        }
        {
            UiPresenter again(renderer, root, read(argv[2]));
            again.reset("second", 1);
        }
        {
            const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() /
                                "samples/reference_game/reference.rml";
            std::filesystem::copy_file(source, root / "reference.rml");
            const auto reference = register_ui_document(root, "reference.rml");
            auto sample = state;
            sample["session"] = "reference";
            sample["generation"] = 1u;
            sample["revision"] = 1u;
            sample["documents"] =
                Json::array({{{"entity", entity},
                              {"asset", reference.id},
                              {"instance", entity.str() + ":" + reference.id.str()},
                              {"visible", true},
                              {"layer", 0u},
                              {"commands", {"Reference"}},
                              {"model",
                               {{"page", "main"},
                                {"message", ""},
                                {"prompt", ""},
                                {"interactions", 0},
                                {"have_save", false},
                                {"binding", "Choose Rebind Jump"},
                                {"conflicts", ""},
                                {"settings_text", "Volume 1.00 | Mouse 1.00 | Gamepad 1.00"}}}}});
            UiPresenter p(renderer, root, read(argv[2]));
            p.reset("reference", 1);
            check(p.accept(sample), p.diagnostic().c_str());
            p.update(1, 1280, 720);
            p.render();
            check(p.diagnostic().empty(), p.diagnostic().c_str());
            auto menu_geometry = [&](float width, float height) {
                Rml::ElementList cards;
                Rml::GetContext(0)->GetDocument(0)->GetElementsByClassName(cards, "card");
                unsigned visible = 0;
                for (auto* card : cards) {
                    if (!card->IsVisible(true))
                        continue;
                    ++visible;
                    const auto size = card->GetBox().GetSize(Rml::BoxArea::Border);
                    const auto offset = card->GetAbsoluteOffset(Rml::BoxArea::Border);
                    check(size.x >= width * .4f && size.y > 100 && offset.x >= 0 && offset.y >= 0 &&
                              offset.x + size.x <= width + 1 && offset.y + size.y <= height + 1,
                          "Reference menu collapsed or extends outside the viewport");
                }
                check(visible == 1, "Reference menu has missing or overlapping cards");
            };
            menu_geometry(1280, 720);
            p.navigate("next");
            p.navigate("accept");
            auto command = p.pending_command();
            check(command && command->at("command") == "Reference" &&
                      command->at("value") == "start",
                  "Controller navigation did not activate the reference New Game control");
            auto ack = *command;
            ack["ok"] = true;
            p.acknowledge(ack);
            double reference_time = 2;
            for (auto page : {"play", "pause", "options"}) {
                sample["revision"] = sample.at("revision").get<unsigned>() + 1;
                sample["documents"][0]["model"]["page"] = page;
                check(p.accept(sample), p.diagnostic().c_str());
                p.update(reference_time += .02, 1280, 720);
                p.render();
                check(p.diagnostic().empty(), p.diagnostic().c_str());
                if (std::string_view(page) == "play")
                    continue;
                menu_geometry(1280, 720);
                p.release_input();
                std::set<std::string> reached;
                for (unsigned index = 0; index < 48; ++index) {
                    p.navigate("next");
                    p.navigate("accept");
                    if (const auto event = p.pending_command()) {
                        reached.insert(event->at("value").get<std::string>());
                        auto response = *event;
                        response["ok"] = true;
                        p.acknowledge(response);
                    }
                    p.update(reference_time += .02, 1280, 720);
                }
                const std::set<std::string> required =
                    std::string_view(page) == "pause"
                        ? std::set<std::string>{"resume", "options", "save", "load", "main", "quit"}
                        : std::set<std::string>{"vsync",        "volume_down",  "volume_up",
                                                "mouse_down",   "mouse_up",     "pad_down",
                                                "pad_up",       "invert_mouse", "invert_pad",
                                                "rebind_jump",  "rebind_apply", "rebind_cancel",
                                                "rebind_clear", "rebind_reset", "back"};
                for (const auto& value : required)
                    check(reached.contains(value),
                          ("Unreachable reference menu control: " + value).c_str());
                p.update(reference_time += .02, 640, 480);
                menu_geometry(640, 480);
                p.update(reference_time += .02, 1280, 720);
            }
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
