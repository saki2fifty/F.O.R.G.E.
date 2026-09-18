#pragma once
#include <forge/geometry.hpp>
#include <stdexcept>
inline void test_geometry() {
    auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    const auto& meshes = forge::primitive_meshes();
    require(meshes[0].size() == 36 && meshes[1].size() == 1728 && meshes[2].size() == 384 &&
                meshes[3].size() == 6,
            "Primitive vertex counts");
    for (const auto& mesh : meshes)
        for (const auto& vertex : mesh) {
            for (float value : vertex.position)
                require(std::isfinite(value) && std::abs(value) <= 0.500001f,
                        "Invalid primitive bounds");
            require(std::abs(forge::geom_dot(vertex.normal, vertex.normal) - 1) < 0.00001f,
                    "Non-unit primitive normal");
        }
    forge::Json entity = {{"id", "mesh"},
                          {"name", "Mesh"},
                          {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}};
    for (unsigned kind = 0; kind < 4; ++kind) {
        entity["components"]["forge.primitive"]["kind"] = kind;
        const auto hit = kind == 3 ? forge::object_hit(entity, {0, 2, 0}, {0, -1, 0}, .05f, 100)
                                   : forge::object_hit(entity, {0, 0, -2}, {0, 0, 1}, .05f, 100);
        require(hit && std::abs(*hit - (kind == 3 ? 2 : 1.5f)) < 0.00001f, "Primitive ray hit");
    }
    entity["components"]["forge.primitive"]["kind"] = 1;
    require(!forge::object_hit(entity, {.49f, .49f, -2}, {0, 0, 1}, .05f, 100),
            "Sphere empty corner selected as cube");
    entity["components"]["forge.primitive"]["kind"] = 2;
    require(!forge::object_hit(entity, {0, .51f, -2}, {0, 0, 1}, .05f, 100),
            "Cylinder picked above cap");
    entity["components"]["forge.rotation"] = {{"x", 23}, {"y", 42}, {"z", 17}, {"extra", "keep"}};
    entity["components"]["forge.scale"] = {{"x", 2}, {"y", 3}, {"z", 4}};
    const forge::ObjectTransform transform(entity);
    const auto eye = transform.point({0, 0, -2});
    const auto ray = forge::geom_sub(transform.point({0, 0, -1}), eye);
    const auto transformed = forge::object_hit(entity, eye, ray, .05f, 100);
    require(transformed && std::abs(*transformed - 1.5f) < 0.00001f,
            "Nonuniform transformed picking");
    const auto bounds = forge::object_bounds(entity);
    for (const auto& vertex : meshes[2]) {
        const auto p = transform.point(vertex.position);
        for (unsigned i = 0; i < 3; ++i)
            require(p[i] >= bounds.first[i] && p[i] <= bounds.second[i],
                    "Bounds omit transformed vertex");
    }
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    forge::Json doc = {{"version", 1}, {"entities", forge::Json::array({entity})}};
    scene.replace(doc);
    const auto unchanged_identity = scene.document();
    require(scene.document() == forge::migrate_scene(doc, &unchanged_identity),
            "New components/unknown data round trip");
    require(scene.schema()["components"].size() == 15 &&
                scene.schema()["components"][1]["id"] == "forge.local_rotation",
            "Reflected transform schema");
    const auto unchanged = scene.document();
    for (auto bad : {0.0f, -1.0f, 10001.0f}) {
        auto invalid = doc;
        invalid["entities"][0]["components"]["forge.scale"]["x"] = bad;
        bool rejected = false;
        try {
            scene.replace(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && scene.document() == unchanged, "Invalid scale replaced scene");
    }
    for (auto kind :
         {forge::Json(4), forge::Json(-1), forge::Json(1.5), forge::Json(4294967296ULL)}) {
        auto invalid = doc;
        invalid["entities"][0]["components"]["forge.primitive"]["kind"] = kind;
        bool rejected = false;
        try {
            scene.replace(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && scene.document() == unchanged, "Invalid primitive kind replaced scene");
    }
    entity["prefab"] = true;
    doc["entities"] = forge::Json::array({entity,
                                          {{"id", "instance"},
                                           {"name", "Instance"},
                                           {"base", "mesh"},
                                           {"components", forge::Json::object()}}});
    scene.replace(doc);
    const auto rendered = scene.effective_document();
    require(rendered["entities"][1]["components"]["forge.scale"]["x"] == 2 &&
                scene.document()["entities"][1]["components"].empty(),
            "Preview materialized authored inheritance");
    auto handles = scene.world().query<forge::StableId>();
    bool inherited = false;
    handles.each([&](flecs::entity e, const forge::StableId& id) {
        if (id.value == scene.canonical_id("instance"))
            inherited = e.has<forge::LocalRotation>() && !e.owns<forge::LocalRotation>() &&
                        e.get<forge::LocalScale>().z == 4;
    });
    require(inherited, "Flecs transform inheritance missing");
}
