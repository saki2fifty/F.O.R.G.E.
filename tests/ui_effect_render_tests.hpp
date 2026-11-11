#pragma once
#include "asset_bytes.hpp"
#include "material_authoring.hpp"
#include "mesh_render_host.hpp"
#include "runtime_dependencies.hpp"
#include "runtime_package.hpp"
#include "shader_authoring.hpp"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
#include "ui_effect_fixture.hpp"
#include "ui_inspection.hpp"
#include <array>
#include <cmath>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>
#include <thread>

namespace forge::test_ui_effect {
inline void check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline void write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(bool(output.write(text.data(), std::streamsize(text.size()))) && bool(output.flush()),
          "UI effect fixture write failed");
}
inline AssetImportOutcome finish(AssetImportService& service) {
    using namespace std::chrono_literals;
    check(service.wait_idle(65s), "UI effect import did not finish");
    auto outcomes = service.poll();
    check(outcomes.size() == 1, "UI effect import receipt missing");
    return std::move(outcomes.front());
}
template <class Read8, class Read16, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              const std::filesystem::path& images, const std::filesystem::path& font,
              const std::filesystem::path& worker, const std::filesystem::path& inspector,
              Read8 read8, Read16 read16, Save save) {
    using namespace Diligent;
    using namespace asset_detail;
    using namespace std::chrono_literals;
    const auto root = images / ("project-" + AssetId::generate().str());
    std::filesystem::create_directories(root / "Shaders");
    const auto shader = AssetId::generate();
    write(root / "effect.shader.json", test::ui_effect_document(shader).dump());
    write(root / "Shaders/ui.hlsl", test::ui_effect_source);
    auto material = MaterialSource::create(AssetId::generate());
    material.document["version"] = 2;
    material.document["overrides"]["shader"] = AssetRef<ShaderAsset>{shader};
    write(root / "effect.material.json", material.document.dump());
    AssetCatalog catalog(root);
    catalog.add({shader, "shader", "effect.shader.json", 1, {}});
    catalog.add({material.asset(), "material", "effect.material.json", 1, {}});
    catalog.save(AssetCatalog::project_index(root));
    auto lease = std::make_shared<ProjectLease>(root);
    {
        auto registry = std::make_shared<AssetImporterRegistry>();
        registry->add(shader_importer(
            worker, {diligent_shader_compiler_digest(), diligent_shader_compiler_debug()}));
        registry->seal();
        AssetImportService shaders(lease, registry, {"windows-x64", "d3d12", "fxc-5.1"}, 1);
        shaders.submit(
            shaders.prepare("effect.shader.json"),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_shader_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        const auto compiled = finish(shaders);
        check(compiled.published, "UI effect native compilation failed: " + compiled.diagnostic);
    }
    auto materials = std::make_unique<AssetImportService>(
        lease, material_import_registry(), ImportTarget{"windows-x64", "d3d12", "fxc-5.1"}, 1);
    const auto publish_material = [&] {
        write(root / "effect.material.json", material.document.dump());
        materials->submit(
            materials->prepare("effect.material.json"),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_material_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        return finish(*materials);
    };
    const auto published = publish_material();
    check(published.published, "UI effect Material publication failed: " + published.diagnostic);
    write(root / "Effect.rml",
          "<rml><head><style>body,div{display:block;}body{margin:0;}"
          "#clip{position:absolute;left:20px;top:20px;width:120px;height:60px;overflow:hidden;}"
          "#effect{width:160px;height:80px;opacity:.5;decorator:shader(\"material:" +
              material.asset().str() +
              "\");}</style></head><body><div id=\"clip\"><div id=\"effect\"/></div></body></rml>");
    const auto document = register_ui_document(root, "Effect.rml");
    catalog = AssetCatalog::open_project(root);
    catalog = declare_runtime_dependencies(
        *lease, document.id,
        {{material.asset(), "material", AssetDependencyKind::Runtime, "declared:UI effect", {}}},
        catalog.document());
    const auto font_bytes = read_bytes(font, 4 * 1024 * 1024);
    const auto run = [&](const std::filesystem::path& project, AssetCatalog selected,
                         bool replace) {
        DiligentPresentation presentation(device);
        auto host = std::make_shared<MeshResourceHost>(presentation, context, project);
        host->catalog(std::make_shared<const AssetCatalog>(std::move(selected)));
        UiDiligentRenderer renderer(device);
        renderer.resources(host);
        UiPresenter presenter(renderer.render_interface(), project, font_bytes);
        presenter.reset("ui-effect", 1);
        const auto entity = EntityId::generate();
        const nlohmann::json initial_state = {
            {"version", 1},
            {"session", "ui-effect"},
            {"generation", 1u},
            {"revision", 1u},
            {"documents", nlohmann::json::array({{{"entity", entity},
                                                  {"asset", document.id},
                                                  {"instance", entity.str()},
                                                  {"visible", true},
                                                  {"layer", 0u},
                                                  {"model", {{"tick", 0u}, {"paused", true}}},
                                                  {"commands", {"Pause", "Resume", "Step"}}}})}};
        check(presenter.accept(initial_state), presenter.diagnostic());
        presenter.update(0, 320, 160);
        const auto ready = [&] {
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (!renderer.resources_ready()) {
                check(std::chrono::steady_clock::now() < deadline, "UI Material resource stalled");
                host->pump();
                std::this_thread::sleep_for(1ms);
            }
        };
        ready();
        const auto draw = [&](bool linear, bool green, bool timed = false) {
            TextureDesc desc;
            desc.Name = "FORGE project UI effect oracle";
            desc.Type = RESOURCE_DIM_TEX_2D;
            desc.Width = 320;
            desc.Height = 160;
            desc.Format = linear ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            RefCntAutoPtr<ITexture> target;
            device->CreateTexture(desc, nullptr, &target);
            check(bool(target), "UI effect target unavailable");
            auto* view = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            context->SetRenderTargets(1, &view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float background[]{0, 0, 1, 1};
            context->ClearRenderTarget(view, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, 320, 160);
            presenter.render();
            renderer.end();
            check(presenter.diagnostic().empty(), presenter.diagnostic());
            const auto bytes = linear ? decltype(read8(view)){} : read8(view);
            const auto halves = linear ? read16(view) : decltype(read16(view)){};
            const auto srgb = [](double value) {
                return value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055;
            };
            for (const auto point : {std::pair{60, 40}, {100, 60}, {150, 40}, {60, 90}, {10, 10}}) {
                const auto [x, y] = point;
                const bool inside = x >= 20 && x < 140 && y >= 20 && y < 80;
                std::array<double, 4> expected{0, 0, 1, 1};
                if (inside) {
                    const double alpha = green ? .25 : .5;
                    const auto u = (x + .5 - 20) / 160 + (timed ? .25 : 0);
                    std::array<double, 3> color{green ? 0 : u - std::floor(u), (y + .5 - 20) / 80,
                                                green ? 0. : 1.};
                    for (unsigned c = 0; c < 3; ++c)
                        expected[c] = (linear ? color[c] : srgb(color[c])) * alpha +
                                      background[c] * (1 - alpha);
                }
                for (unsigned c = 0; c < 4; ++c) {
                    const auto actual = linear ? double(float(halves[y * 320 + x][c]))
                                               : bytes[y * 320 + x][c] / 255.;
                    check(std::abs(actual - expected[c]) < (linear ? .004 : .015),
                          "UI effect UV/size/time, clipping, opacity, parameter or color-space "
                          "mismatch");
                }
            }
            if (!linear)
                save(bytes, 320, 160,
                     images / (timed     ? "effect-time.ppm"
                               : replace ? (green ? "effect-replaced.ppm" : "effect-initial.ppm")
                                         : "effect-relocated.ppm"));
        };
        for (const bool linear : {false, true})
            draw(linear, !replace);
        if (replace) {
            material.document["overrides"]["parameters"]["speed"] = {
                {"type", unsigned(MaterialParameterType::Scalar)}, {"value", {.125}}};
            const auto animated = publish_material();
            check(animated.published,
                  "UI presentation-time Material failed: " + animated.diagnostic);
            host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
            ready();
            // The model tick stays zero; only native presentation time advances.
            presenter.update(2, 320, 160);
            for (const bool linear : {false, true})
                draw(linear, false, true);
            material.document["overrides"]["parameters"].erase("speed");
            material.document["overrides"]["parameters"]["tint"] = {
                {"type", unsigned(MaterialParameterType::LinearColor4)}, {"value", {0, 1, 0, .5}}};
            const auto changed = publish_material();
            check(changed.published, "UI parameter replacement failed: " + changed.diagnostic);
            host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
            ready();
            for (const bool linear : {false, true})
                draw(linear, true);
            const auto before =
                read_bytes(AssetCatalog::project_index(root), max_asset_index_bytes);
            material.document["overrides"]["parameters"]["tint"]["type"] =
                unsigned(MaterialParameterType::Scalar);
            const auto invalid = publish_material();
            check(!invalid.published && before == read_bytes(AssetCatalog::project_index(root),
                                                             max_asset_index_bytes),
                  "Rejected UI Material changed its last-good catalog");
            draw(false, true);
            material.document["overrides"]["parameters"]["tint"]["type"] =
                unsigned(MaterialParameterType::LinearColor4);
            write(root / "effect.material.json", material.document.dump());
            auto broken = AssetCatalog::open_project(root);
            auto record = broken.records().at(material.asset());
            record.metadata["forge.import"]["generation"] =
                record.metadata.at("forge.import").at("generation").get<std::uint64_t>() + 1;
            record.metadata["forge.import"]["key"] = std::string(64, 'f');
            broken.replace(record);
            host->catalog(std::make_shared<const AssetCatalog>(std::move(broken)));
            bool failed = false;
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            while (!failed) {
                try {
                    (void)renderer.resources_ready();
                } catch (const std::exception&) {
                    failed = true;
                }
                check(std::chrono::steady_clock::now() < deadline,
                      "Missing UI Material revision did not report terminal failure");
                host->pump();
                std::this_thread::sleep_for(1ms);
            }
            write(root / "Plain.rml",
                  "<rml><head><style>body,div{display:block;}div{width:80px;height:40px;"
                  "background-color:red;}</style></head><body><div/></body></rml>");
            const auto plain = register_ui_document(root, "Plain.rml");
            auto next = initial_state;
            next["documents"][0]["asset"] = plain.id;
            const auto ticket = presenter.prepare(next);
            check(presenter.prepared(ticket) && renderer.resources_ready(presenter, ticket),
                  "Failed live effect blocked an unrelated prepared UI scene");
            presenter.cancel_prepared(ticket);
            host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
            ready();
            draw(false, true);
        }
    };
    run(root, catalog, true);
    materials.reset();
    lease.reset();
    const auto packaged = images / ("content-" + document.id.str());
    const std::array roots{document.id};
    (void)package_runtime_content(root, packaged, roots, {"windows", "d3d12"}, {}, {}, {},
                                  [&](AssetId asset, std::stop_token stop) {
                                      return inspect_ui_dependencies(inspector, font, root, asset,
                                                                     stop);
                                  });
    const auto relocated = images / ("relocated-content-" + document.id.str());
    std::filesystem::rename(packaged, relocated);
    const auto cooked = open_runtime_content(relocated, {"windows", "d3d12"});
    check(cooked.records().contains(material.asset()) &&
              !std::filesystem::exists(relocated / "Shaders") &&
              !std::filesystem::exists(relocated / "effect.shader.json") &&
              !std::filesystem::exists(relocated / "effect.material.json"),
          "UI effect package lost its Material closure or retained authored sources");
    std::filesystem::rename(root, root.string() + "-offline");
    run(relocated, cooked, false);
    context->WaitForIdle();
}
} // namespace forge::test_ui_effect
