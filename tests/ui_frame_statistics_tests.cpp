#include "ui_test_renderer.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <forge/ui_presenter.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<std::byte> read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Font could not be opened");
    const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected scratch root and font");
        std::filesystem::create_directories(argv[1]);
        UiTestRenderer renderer;
        {
            UiPresenter presenter(renderer, argv[1], read(argv[2]));
            presenter.reset("frames", 1);
            nlohmann::json state{{"version", 1},
                                 {"session", "frames"},
                                 {"generation", 1u},
                                 {"revision", 1u},
                                 {"documents", nlohmann::json::array()}};
            check(presenter.accept(state), presenter.diagnostic().c_str());
            presenter.update(1, 800, 600);
            presenter.render();
            auto* context = Rml::GetContext(0);
            check(context && context->GetNumDocuments() == 0 && renderer.draws == 0,
                  "Disabled statistics created a document or draw");
            presenter.frame_statistics("FPS 60 | Frame 16.67 ms | Peak 19.00 ms");
            presenter.update(2, 800, 600);
            presenter.render();
            check(renderer.draws > 0 && context->GetNumDocuments() == 1,
                  "Statistics did not render without an authored UI canvas");
            check(presenter.document_count() == 0 && presenter.asset_snapshot() &&
                      presenter.asset_snapshot()->documents.empty() &&
                      presenter.asset_snapshot()->sources.empty(),
                  "Host statistics entered authored documents or asset provenance");
            auto* document = context->GetDocument(0);
            auto* panel = document->GetElementById("forge-frame-statistics");
            check(panel && panel->GetNumChildren() == 1, "Statistics text node missing");
            auto* text = static_cast<Rml::ElementText*>(panel->GetChild(0));
            check(!presenter.mouse_move(15, 15, 0) && !presenter.mouse_button(0, true, 0) &&
                      !presenter.mouse_button(0, false, 0) && !presenter.wants_text() &&
                      !presenter.pending_command(),
                  "Statistics took gameplay input or queued a command");
            const std::string literal = "FPS <literal> & {{unbound}}";
            presenter.frame_statistics(literal);
            presenter.update(3, 800, 600);
            check(text->GetText() == literal && panel->GetNumChildren() == 1,
                  "Host statistics text was interpreted as markup or model data");
            for (const auto invalid : {std::string(129, 'x'), std::string("bad\xff")}) {
                bool rejected = false;
                try {
                    presenter.frame_statistics(invalid);
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected && text->GetText() == literal,
                      "Invalid host statistics changed the old reading");
            }
            bool rejected_thread = false;
            std::thread other([&] {
                try {
                    presenter.frame_statistics("wrong thread");
                } catch (const std::exception&) {
                    rejected_thread = true;
                }
            });
            other.join();
            check(rejected_thread, "Statistics changed off the UI owner thread");
            for (int i = 0; i < 100; ++i) {
                presenter.frame_statistics("");
                presenter.frame_statistics(literal);
            }
            check(context->GetNumDocuments() == 1, "Statistics toggles accumulated documents");
            presenter.frame_statistics("");
            presenter.update(4, 800, 600);
            const auto before = renderer.draws;
            presenter.render();
            check(renderer.draws == before, "Hidden statistics continued drawing");
            presenter.frame_statistics("FPS -- | Frame -- ms");
            state["revision"] = 2u;
            const auto ticket = presenter.prepare(state);
            check(presenter.prepared(ticket) && Rml::GetNumContexts() == 2,
                  "Statistics interfered with UI candidate preparation");
            presenter.frame_statistics("FPS 30 | Frame 33.33 ms");
            check(presenter.activate_prepared(ticket),
                  "Prepared statistics context did not publish");
            presenter.update(5, 1280, 720, 2);
            presenter.render();
            check(presenter.document_count() == 0 && Rml::GetNumContexts() == 1,
                  "Statistics replaced the authored document count or retained old context");
            presenter.reset("replacement", 2);
            state["session"] = "replacement";
            state["generation"] = 2u;
            state["revision"] = 1u;
            check(presenter.accept(state), presenter.diagnostic().c_str());
            presenter.update(6, 400, 300);
            presenter.render();
            check(Rml::GetNumContexts() == 1 && presenter.document_count() == 0,
                  "Statistics host state failed scene/session replacement");
        }
        check(renderer.geometry.empty() && renderer.textures.empty(),
              "Statistics retained geometry/textures after presenter destruction");
        std::cout << "Native host statistics text, no-canvas rendering, literal/admission/input, "
                     "bounded toggles, candidate/reset and resource retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
