#pragma once
#include "content_view.hpp"
#include <iostream>
inline void test_content_browser(const std::filesystem::path& root) {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    AssetCatalog catalog(root);
    const auto model = AssetId::generate(), member = AssetId::generate(),
               removed = AssetId::generate();
    AssetRecord owner{model, "model", "Assets/Models/Car.glb"};
    owner.metadata["forge.import"] = {{"source_digest", "good"}, {"key", std::string(64, 'a')}};
    catalog.add(owner);
    AssetRecord mesh{member, "mesh", owner.source};
    mesh.subasset = AssetSubasset{model, "durable-entry", false};
    mesh.metadata["forge.model"] = {{"name", "Front Wheel"}};
    catalog.add(mesh);
    auto tombstone = mesh;
    tombstone.id = removed;
    tombstone.subasset->key = "removed-entry";
    tombstone.subasset->removed = true;
    catalog.add(tombstone);
    SourceSnapshot sources;
    sources.files[owner.source] = {owner.source, "good", 42, {}, "model", {}};
    const std::filesystem::path unimported = "Assets/ModelsExtra/image.PNG";
    sources.files[unimported] = {unimported, "new", 12, {}, "texture", {}};
    auto index = ContentIndex::build(catalog, &sources);
    ContentQuery query;
    query.text = "FRONT glb published";
    check(index.query(query).size() == 1,
          "Name/path/type/state search did not combine case-insensitive words");
    query = {};
    query.folder = "Assets/Models";
    check(index.query(query).size() == 3, "Folder prefix matched ModelsExtra or omitted members");
    query = {};
    query.state = ContentState::Unimported;
    check(index.query(query).size() == 1, "Unimported status was lost");
    check(index.folders.at("") == std::vector<std::string>{"Assets"} &&
              index.folders.at("Assets").size() == 2,
          "Folder tree omitted ancestors or duplicated source owners");
    sources.files[owner.source].digest = "changed";
    index = ContentIndex::build(catalog, &sources);
    query.state = ContentState::Changed;
    check(index.query(query).size() == 2,
          "Owner source change did not propagate to active member status");
    sources.files.erase(owner.source);
    index = ContentIndex::build(catalog, &sources);
    query.state = ContentState::Missing;
    check(index.query(query).size() == 2, "Complete scan did not show missing source");
    sources.complete = false;
    index = ContentIndex::build(catalog, &sources);
    check(index.query(query).empty(), "Incomplete scan inferred missing assets");
    index = ContentIndex::build(catalog, &sources, {}, {{model, ContentState::Failed}});
    query = {};
    query.state = ContentState::Failed;
    check(index.query(query).size() == 2,
          "Failed owner import was not projected to active generated members");
    query.state = ContentState::Removed;
    check(index.query(query).size() == 1, "Job state hid a removed member diagnostic");
    ContentLocations locations;
    locations.visit("Assets");
    locations.visit("Assets/Models");
    locations.back();
    check(locations.current() == "Assets" && locations.forward_available(),
          "Back lost navigation history");
    locations.visit("Assets/ModelsExtra");
    check(!locations.forward_available(), "New location retained stale forward navigation");
    locations.back();
    locations.forward();
    check(locations.current() == "Assets/ModelsExtra", "Forward did not restore location");
    sources.complete = true;
    for (int i = 0; i < 10000; ++i) {
        const auto path =
            std::filesystem::path("Assets/Large") / ("Texture-" + std::to_string(i) + ".png");
        sources.files[path] = {path, "digest", 8, {}, "texture", {}};
    }
    const auto start = std::chrono::steady_clock::now();
    auto many = std::make_shared<const ContentIndex>(ContentIndex::build(catalog, &sources));
    query = {};
    query.text = "texture .png";
    const auto found = many->query(query);
    check(found.size() == 10001, "Large browser query omitted source rows");
    std::cout << "Content 10004-row index+query: "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                     .count()
              << " ms\n";
    ContentView view;
    view.update(many);
    ui::EditorSelection selection;
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Browser-test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        view.draw(selection, false);
        ImGui::End();
        ImGui::Render();
    };
    for (float scale : {1.f, 2.f}) {
        ui::style(scale);
        io.DisplaySize = scale == 1 ? ImVec2{1440, 900} : ImVec2{960, 640};
        frame();
        frame();
        check(ImGui::GetDrawData()->TotalVtxCount < 20000,
              "Browser submitted all large-project rows instead of clipping");
    }
    ui::style(1.f);
    io.DisplaySize = {1440, 900};
    frame();
    ImGuiWindow* results = nullptr;
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (std::string(window->Name).find("Browser-test/content-results") != std::string::npos)
            results = window;
    check(results != nullptr, "Browser result child was not created");
    const auto first = results->DC.CursorStartPos;
    const auto click = [&](int row, bool ctrl, bool shift) {
        const float stride =
            ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddMousePosEvent(first.x + 30, first.y + row * stride + 5);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
    };
    click(0, false, false);
    check(view.selection().size() == 1, "Mouse selection did not reach Content");
    click(2, true, false);
    check(view.selection().size() == 2, "Ctrl-click did not extend Content selection");
    click(4, false, true);
    check(view.selection().size() == 3, "Shift-click did not select the anchored range");
    const auto selected = view.selection();
    view.update(std::make_shared<const ContentIndex>(*many));
    frame();
    check(view.selection() == selected, "Background projection replacement lost stable selection");
    click(6, false, true);
    check(view.selection().size() == 5, "Unchanged refresh reset the native Shift range anchor");
    view.load_settings({{"grid", true}, {"tile_size", 99999}, {"folder_tree", false}});
    check(view.settings().at("tile_size") == 220 && view.settings().at("grid").get<bool>(),
          "Content preferences were not bounded/restored");
    for (float scale : {1.f, 2.f}) {
        ui::style(scale);
        io.DisplaySize = scale == 1 ? ImVec2{1440, 900} : ImVec2{960, 640};
        frame();
        frame();
        check(ImGui::GetDrawData()->TotalVtxCount < 20000, "Grid did not clip large content");
    }
    view.load_settings({{"grid", false}, {"folder_tree", true}});
    view.update(std::make_shared<const ContentIndex>(ContentIndex::build(catalog, nullptr)));
    ui::style(1.f);
    io.DisplaySize = {1440, 900};
    frame();
    frame();
    selection.select_entity("keep-entity-inspector");
    const auto drag_first = results->DC.CursorStartPos;
    io.AddMousePosEvent(drag_first.x + 30, drag_first.y + 5);
    io.AddMouseButtonEvent(0, true);
    frame();
    check(selection.entity() == "keep-entity-inspector",
          "Mouse press replaced Inspector before asset drag");
    io.AddMousePosEvent(drag_first.x + 80, drag_first.y + 5);
    frame();
    check(ImGui::GetDragDropPayload() != nullptr && selection.entity() == "keep-entity-inspector",
          "Asset drag lost entity Inspector or payload");
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    frame();
}
