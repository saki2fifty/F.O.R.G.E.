#pragma once
#include <forge/authoring.hpp>
void check(bool value, const char* why);
inline void test_bulk_authoring_limits() {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    const std::string id =
        authoring_command(scene, "entity.create", {{"name", "A"}}).at("selected");
    auto source = scene.document();
    scene.reset(source);
    auto rename = [&](const std::string& name) {
        return Json{{"operation", "entity.rename"},
                    {"arguments", {{"entity", id}, {"name", name}}}};
    };
    auto commands = Json::array();
    for (unsigned i = 0; i < 128; ++i)
        commands.push_back(rename("Batch " + std::to_string(i)));
    const auto revision = scene.revision();
    const auto result = apply_authoring(scene, commands, revision);
    check(result.at("results").size() == 128 && scene.revision() == revision + 1 &&
              scene.document()["entities"][0]["name"] == "Batch 127",
          "Maximum supported command batch did not publish one revision");
    check(scene.undo() && scene.document() == source && !scene.can_undo(),
          "Maximum command batch did not remain one Undo step");
    commands.push_back(rename("Must not publish"));
    auto rejected_batch = [&](const Json& batch, const std::string& expected_code) {
        const auto before = scene.document();
        const auto rev = scene.revision();
        const bool undo = scene.can_undo(), redo = scene.can_redo();
        AuthoringSession api(scene);
        const auto response = api.handle({{"api", 1},
                                          {"method", "scene.apply"},
                                          {"target", api.target()},
                                          {"expected_revision", rev},
                                          {"commands", batch}});
        check(!response.at("ok") && response.at("error").at("code") == expected_code &&
                  scene.document() == before && scene.revision() == rev &&
                  scene.can_undo() == undo && scene.can_redo() == redo,
              "Rejected command batch changed content, revision, history or error "
              "classification");
    };
    rejected_batch(commands, "invalid_arguments");

    // Use the actual serialized bytes, including escaped opaque text. The
    // following shrink must never excuse an oversized intermediate state.
    constexpr std::size_t budget = 8 * 1024 * 1024;
    auto& opaque = source["entities"][0]["components"]["missing.budget"];
    opaque = {{"escaped", "\"\\\t\n"}, {"data", ""}};
    opaque["data"] = std::string(budget - source.dump().size(), 'x');
    scene.reset(source);
    check(scene.document().dump().size() == budget, "Exact serialized budget fixture drifted");
    const auto no_op_revision = scene.revision();
    const auto no_op = apply_authoring(scene, Json::array({rename("A")}), no_op_revision);
    check(!no_op.at("changed") && scene.revision() == no_op_revision && !scene.can_undo(),
          "Exact byte limit incorrectly rejected or published a semantic no-op");
    rejected_batch(Json::array({rename("AA"), rename("A")}), "limit_exceeded");
    opaque["data"].get_ref<std::string&>().pop_back();
    scene.reset(source);
    apply_authoring(scene, Json::array({rename("AA")}), scene.revision());
    check(scene.document().dump().size() == budget &&
              scene.document()["entities"][0]["components"]["missing.budget"] == opaque &&
              scene.undo() && scene.document() == source,
          "Exact-limit commit or its Undo changed opaque serialized data");

    auto full = source;
    full["entities"] = Json::array();
    for (unsigned i = 0; i < 10000; ++i)
        full["entities"].push_back({{"id", EntityId::generate().str()},
                                    {"name", "Existing"},
                                    {"components", Json::object()}});
    scene.reset(full);
    rejected_batch(Json::array({{{"operation", "entity.create"}},
                                {{"operation", "entity.delete"},
                                 {"arguments", {{"entity", full["entities"][0]["id"]}}}}}),
                   "limit_exceeded");
}
