#pragma once
#include "asset_bytes.hpp"
#include "ui_markup_authoring.hpp"
#include <algorithm>
#include <cmath>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_preview_pixels {
using Json = nlohmann::json;
inline void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class Read, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              const std::filesystem::path& output, const std::filesystem::path& font_path,
              const std::filesystem::path& gallery, Read read, Save save) {
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    const auto text = [&](const char* name) {
        const auto bytes =
            asset_detail::read_bytes(gallery / "Assets/UI/Layout" / name, 256 * 1024);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto stylesheet = text("layout.rcss");
    const auto draft = text("layout.rml");
    const auto nodes = inspect_ui_markup(draft);
    std::size_t panel_index = UiMarkupElement::no_parent;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        for (const auto& attr : nodes[i].attributes)
            if (attr.name == "id" &&
                draft.substr(attr.value.begin, attr.value.end - attr.value.begin) == "panel")
                panel_index = i;
    require(panel_index != UiMarkupElement::no_parent, "Preview fixture panel missing");
    Json evidence = Json::array();
    for (const bool shaping : {false, true}) {
        const auto directory = output / (shaping ? "shaped" : "default");
        const auto project = directory / "project";
        std::filesystem::create_directories(project / "Assets");
        const auto source = project / "Assets/preview.rml";
        const auto css = project / "Assets/layout.rcss";
        const std::string saved = "<rml><body>Saved baseline</body></rml>";
        std::ofstream(source) << saved;
        const auto baseline = asset_detail::read_bytes(source, 65536);
        std::ofstream(css) << stylesheet;
        const auto asset = register_ui_document(project, "Assets/preview.rml");
        const auto catalog = asset_detail::read_bytes(project / "forge.assets.json", 1024 * 1024);
        using Pixels =
            decltype(read(device, context, static_cast<Diligent::ITextureView*>(nullptr)));
        Pixels original;
        std::shared_ptr<Diligent::ITextureView> retained;
        {
            UiDiligentRenderer renderer(device);
            UiPresenter presenter(renderer.render_interface(), project, font, {}, nullptr, shaping);
            const auto render = [&](const std::string& text, int width, int height,
                                    const std::string& name) {
                require(presenter.preview_document({asset.id}, "Assets/preview.rml", text, width,
                                                   height),
                        "Native source preview candidate failed");
                auto view =
                    renderer.draw_preview(presenter, context, unsigned(width), unsigned(height));
                auto pixels = read(device, context, view.get());
                require(pixels.size() == std::size_t(width) * height,
                        "Preview readback dimensions");
                require(std::all_of(pixels.begin(), pixels.end(),
                                    [](const auto& p) { return p[3] == 255; }),
                        "Preview image is not opaque for editor composition");
                if (!name.empty())
                    save(pixels, unsigned(width), unsigned(height), directory / (name + ".ppm"));
                return std::pair{std::move(view), std::move(pixels)};
            };
            auto initial = render(draft, 1000, 700, "initial");
            original = initial.second;
            const auto blue = [&](const Pixels& pixels, unsigned stride, unsigned x, unsigned y) {
                const auto& p = pixels.at(y * stride + x);
                return std::abs(int(p[0]) - 37) <= 2 && std::abs(int(p[1]) - 99) <= 2 &&
                       std::abs(int(p[2]) - 235) <= 2;
            };
            require(blue(original, 1000, 340, 290),
                    "Initial panel pixels do not match native layout");
            require(
                !presenter.preview_document({asset.id}, "Assets/preview.rml", "<rml>", 1000, 700),
                "Invalid source preview candidate replaced last-good document");
            bool rejected = false;
            try {
                (void)renderer.draw_preview(presenter, context, 1000, 700);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && read(device, context, initial.first.get()) == original,
                    "Failed preview rendering overwrote last-good pixels");
            const auto moved = set_ui_markup_layout(draft, panel_index, {150, 180, 250, 190});
            auto changed = render(moved, 1000, 700, "moved");
            require(changed.second != original && blue(changed.second, 1000, 380, 350) &&
                        !blue(changed.second, 1000, 65, 100),
                    "Move/resize source values did not change the expected pixels");
            auto restored = render(draft, 1000, 700, "");
            require(restored.second == original,
                    "Restoring source did not restore exact preview pixels");
            auto narrow = render(draft, 420, 700, "narrow");
            require(blue(narrow.second, 420, 340, 290),
                    "Narrow preview changed absolute pixel layout");
            std::ofstream(css) << stylesheet + " body {background-color:#283544;}";
            auto themed = render(draft, 1000, 700, "saved-stylesheet");
            require(themed.second.at(5 * 1000 + 5) != original.at(5 * 1000 + 5),
                    "Fresh preview did not reload saved linked stylesheet");
            std::ofstream(css) << stylesheet;
            auto final = render(draft, 1000, 700, "");
            require(final.second == original, "Saved dependency restore changed preview pixels");
            retained = std::move(final.first);
        }
        require(read(device, context, retained.get()) == original,
                "Retiring native presenter/backend invalidated the retained image");
        require(asset_detail::read_bytes(project / "forge.assets.json", 1024 * 1024) == catalog &&
                    asset_detail::read_bytes(source, 65536) == baseline,
                "Preview wrote saved source or catalog");
        const auto relocated = directory / "relocated";
        std::filesystem::copy(project, relocated,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        {
            UiDiligentRenderer renderer(device);
            UiPresenter presenter(renderer.render_interface(), relocated, font, {}, nullptr,
                                  shaping);
            require(presenter.preview_document({asset.id}, "Assets/preview.rml", draft, 1000, 700),
                    "Relocated draft preview could not resolve its registered source/dependency");
            auto view = renderer.draw_preview(presenter, context, 1000, 700);
            auto pixels = read(device, context, view.get());
            require(pixels == original, "Relocating the project changed draft preview pixels");
            save(pixels, 1000, 700, directory / "relocated.ppm");
        }
        evidence.push_back({{"shaping", shaping},
                            {"frames", 7},
                            {"exact_restore_pairs", 2},
                            {"relocated_exact_pair", true},
                            {"opaque", true},
                            {"retained_after_retirement", true},
                            {"failed_candidate_preserved", true}});
    }
    std::ofstream(output / "preview-evidence.json") << evidence.dump(2) << '\n';
}
} // namespace forge::test_ui_preview_pixels
