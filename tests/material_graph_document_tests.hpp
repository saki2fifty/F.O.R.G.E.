#pragma once
#include "material_graph_canvas.hpp"
#include "material_graph_document.hpp"
namespace forge::test {
inline void material_graph_documents(SceneDocument& project, const Scene& scene) {
    const auto check = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto rejected = [&](auto f) {
        bool threw = false;
        try {
            f();
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "Invalid graph source operation accepted");
    };
    const auto before = scene.document();
    auto document =
        MaterialGraphDocument::create(project.writer_guard(), "Assets/history.shader.json");
    const auto source = document->source().document;
    const auto revision = document->revision();
    document->edit(revision, "Move node",
                   [](auto& j) { j["graph"]["nodes"][0]["position"] = {140, 220}; });
    check(document->dirty() && document->can_undo(), "Graph move lacks document history");
    rejected([&] { document->edit(revision, "Stale", [](auto&) {}); });
    document->undo();
    check(document->source().document == source && !document->dirty(),
          "Graph Undo did not restore saved source");
    document->redo();
    check(document->source().document != source, "Graph Redo lost edit");
    document->save();
    MaterialGraphDocument reopened(project.writer_guard(), document->locator());
    check(reopened.source().document == document->source().document,
          "Graph save/reopen changed source");
    const auto valid = document->source().document;
    rejected([&] {
        document->edit(document->revision(), "Change identity",
                       [](auto& j) { j["asset_id"] = AssetId::generate(); });
    });
    check(document->source().document == valid, "Rejected graph edit partially committed");
    document->edit(document->revision(), "Unknown node", [](auto& j) {
        j["graph"]["nodes"][0]["type"] = "plugin.unavailable";
        j["graph"]["nodes"][0]["opaque"] = {{"unrecognized", 42}};
    });
    document->save();
    MaterialGraphDocument unknown(project.writer_guard(), document->locator());
    check(unknown.source().document == document->source().document,
          "Missing-schema save lost opaque data");
    rejected([&] { compile_material_graph(unknown.source().document.at("graph")); });
    atomic_write(project.project() / document->locator(), valid.dump(2));
    rejected([&] { document->save(); });
    // Exercise actual canvas drawing for unsupported schema and malformed known
    // configuration; the UI must remain balanced and preserve source verbatim.
    MaterialGraphCanvas canvas;
    auto graph = unknown.source().document.at("graph");
    canvas.selection = {graph.at("nodes")[0].at("id").get<GraphNodeId>()};
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::Begin("Graph draw test");
        canvas.draw(
            graph, 1,
            [](auto, const auto&) { throw std::runtime_error("Drawing changed authored source"); },
            false);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    graph["nodes"][0]["type"] = "parameter";
    graph["nodes"][0]["data"]["value"] = "bad";
    canvas.reset();
    canvas.selection = {graph.at("nodes")[0].at("id").get<GraphNodeId>()};
    frame();
    check(scene.document() == before, "Graph source/history edited the scene");
}
} // namespace forge::test
