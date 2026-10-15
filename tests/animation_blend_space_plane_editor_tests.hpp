// Included in each scale's real ImGui/document fixture, sharing its input helpers.
{
    const auto plane_path = std::filesystem::path("Assets/PlaneEditing.blendspace.json");
    auto created =
        AnimationBlendSpaceDocument::create(project.writer_guard(), plane_path, [&](AssetId id) {
            auto value = initial;
            value["asset_id"] = id;
            value["samples"][0]["note"] = "source extension";
            return AnimationBlendSpaceSource{value};
        });
    const auto axis_source = created->source().document;
    created.reset();
    editor.open(project, plane_path);
    frame();
    frame();
    click("animation_blend_space:convert_2d");
    const auto converted = editor.document()->source().document;
    require(converted.at("version") == 2 &&
                converted.at("samples")[0].at("id") == Json(sample_id) &&
                converted.at("samples")[0].at("note") == "source extension" &&
                converted.at("samples")[0].at("position").at("y") == 0,
            "2D conversion lost identity, source extensions or initial Y");
    editor.undo();
    frame();
    require(editor.document()->source().document == axis_source, "2D conversion was not one Undo");
    editor.redo();
    frame();
    const auto position_revision = editor.document()->revision();
    number("animation_blend_space:position_y", "0.75");
    require(editor.document()->revision() == position_revision + 1 &&
                editor.document()->source().document.at("samples")[0].at("position").at("y") == .75,
            "XY Enter did not commit one source history entry");
    const auto before_drag = editor.document()->source().document;
    const auto before_drag_revision = editor.document()->revision();
    point("animation_blend_space:plane_point:" + sample_id.str());
    const auto down = io.MousePos;
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(down.x + 35 * scale, down.y - 22 * scale);
    frame();
    require(editor.document()->source().document == before_drag,
            "2D drag wrote source before release");
    io.AddMouseButtonEvent(0, false);
    frame();
    require(editor.document()->revision() == before_drag_revision + 1 &&
                editor.document()->source().document != before_drag &&
                editor.document()->source().document.at("samples")[0].at("id") == Json(sample_id),
            "Plane drag did not commit one source edit with the same sample identity");
    const auto after_drag = editor.document()->source().document;
    editor.undo();
    frame();
    require(editor.document()->source().document == before_drag, "Plane drag Undo differed");
    editor.redo();
    frame();
    require(editor.document()->source().document == after_drag, "Plane drag Redo differed");
    point("animation_blend_space:plane_point:" + sample_id.str());
    const auto escape_down = io.MousePos;
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(escape_down.x - 25 * scale, escape_down.y + 15 * scale);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    io.AddMouseButtonEvent(0, false);
    frame();
    require(editor.document()->source().document == after_drag, "Escape committed a plane drag");
    const auto valid_revision = editor.document()->revision();
    number("animation_blend_space:position_y", "1000001");
    require(editor.document()->revision() == valid_revision && !editor.diagnostic().empty(),
            "Invalid XY coordinate changed source/history");
    editor.request_close();
    frame();
    frame();
    click("button:Discard");
    require(!editor.is_open(), "2D source close guard failed");
    show_create = true;
    frame();
    click("button:New animation blend space...");
    frame();
    number("animation_blend_space:new_path", "Assets/CreatedPlane.blendspace.json");
    click("animation_blend_space:new_dimensions");
    frame();
    click("animation_blend_space:new_dimensions:2");
    click("button:Create");
    show_create = false;
    frame();
    require(editor.document() && editor.document()->source().document.at("version") == 2 &&
                editor.document()->source().document.at("samples").empty(),
            "Actual create dialog did not create a 2D draft: " + editor.diagnostic() +
                (editor.document()
                     ? " version=" + editor.document()->source().document.at("version").dump()
                     : " no document"));
    editor.request_close();
    frame();
    frame();
    click("button:Discard");
    require(!editor.is_open(), "New 2D draft close guard failed");
    editor.open(project, "Assets/MotionPlane.blendspace.json");
    auto attached = entity.get<Animator>();
    attached.blend_position_y = 42;
    entity.set(attached);
    const auto before_attach = scene.document();
    frame();
    frame();
    click("animation_blend_space:attach");
    require(entity.get<Animator>().blend_position == 0 &&
                entity.get<Animator>().blend_position_y == 0 &&
                entity.get<Animator>().blend_space.id == editor.document()->source().asset(),
            "2D source attachment did not initialize both coordinates");
    require(scene.undo() && scene.document() == before_attach,
            "XY attachment was not one Scene Undo");
    editor.request_close();
    frame();
}
