#include <cmath>
#include <forge/input.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid recording operation was accepted");
}
InputMap map() {
    return InputMap(
        {{"version", 2},
         {"contexts", Json::array({{{"name", "Game"}, {"active", true}},
                                   {{"name", "Menu"}, {"active", true}, {"phase", "control"}}})},
         {"actions",
          Json::array({{{"id", "23000000-0000-4230-8230-000000000001"},
                        {"name", "Jump"},
                        {"kind", "digital"},
                        {"context", "Game"},
                        {"bindings", Json::array({{{"control", "key.space"}}})}},
                       {{"id", "23000000-0000-4230-8230-000000000002"},
                        {"name", "Look"},
                        {"kind", "axis1"},
                        {"context", "Game"},
                        {"bindings", Json::array({{{"control", "mouse.delta_x"}, {"x", .5}}})}},
                       {{"id", "23000000-0000-4230-8230-000000000003"},
                        {"name", "Menu"},
                        {"kind", "digital"},
                        {"context", "Menu"},
                        {"bindings", Json::array({{{"control", "key.escape"}}})}}})}});
}
Json cells(const InputSnapshot& snapshot) {
    Json result = Json::array();
    for (const auto& [id, s] : snapshot.actions)
        result.push_back(Json::array({id, s.x, s.y, s.held, s.pressed, s.released}));
    return result;
}
void exercise() {
    RuntimeInput input;
    input.configure(map());
    rejects([&] { input.begin_input_recording("", 60); });
    input.begin_input_recording("source-a", 120);
    rejects([&] { input.finish_input_recording(); });
    check(input.input_recording_status().at("mode") == "recording",
          "Empty Finish discarded the active input recording");
    // A complete quick tap and relative motion arrive while paused: polling and
    // control frames must not create gameplay frames or consume their edges.
    input.submit({{"key.space", 1}, {"key.space", 0}, {"mouse.delta_x", 8}, {"key.escape", 1}});
    input.latch_controls(1);
    check(input.input_recording_status().at("ticks") == 0, "Control frame was recorded");
    std::vector<Json> expected;
    expected.push_back(cells(input.latch(1)));
    input.submit({{"key.space", 1}});
    expected.push_back(cells(input.latch(2)));
    expected.push_back(cells(input.latch(3))); // catch-up tick retains held, consumes no edges
    input.release_all();                       // focus/capture loss: actual release is recorded
    expected.push_back(cells(input.latch(4)));
    input.submit({{"key.space", 1}});
    expected.push_back(cells(input.latch(5)));
    input.finish_input_recording();
    const auto document = input.input_recording_document();
    validate_input_recording(document);
    check(document.at("frames").size() == 5 && document.at("frames")[0].size() == 2 &&
              document.at("frames")[0][0][2] == 6 && document.at("frames")[0][1][0] == 4 &&
              document.at("frames")[1][1][0] == 0,
          "Recorded frames lost tap, motion or domain separation");
    const auto reopened = Json::parse(document.dump());
    RuntimeInput replay;
    replay.configure(map());
    for (const auto& [source, hz] : {std::pair{"source-b", 120.}, {"source-a", 60.}})
        rejects([&] { replay.replay_input_recording(reopened, source, hz); });
    auto bad = reopened;
    bad["frames"][0][0][2] = 8;
    rejects([&] { replay.replay_input_recording(bad, "source-a", 120); });
    check(replay.input_recording_status().at("mode") == "idle",
          "Rejected replay changed runtime state");
    replay.replay_input_recording(reopened, "source-a", 120);
    rejects([&] { replay.begin_rebind(); });
    check(!replay.rebinding(), "Rejected replay rebind changed the listener");
    rejects([&] { replay.replace_map(map()); });
    rejects([&] { replay.latch(2); });
    for (std::size_t i = 0; i < expected.size(); ++i) {
        replay.submit({{"key.space", 0}, {"mouse.delta_x", -999}, {{}, 0, true}});
        replay.latch_controls(i + 1);
        check(cells(replay.latch(i + 1)) == expected[i], "Replay changed a consumed snapshot");
    }
    const auto action = map().actions().front().id;
    check(replay.input_recording_status().at("mode") == "finished" &&
              replay.latch(6).actions.at(action).released &&
              !replay.latch(7).actions.at(action).released,
          "Replay exhaustion did not neutralize held input exactly once");
    check(replay.input_recording_status().at("consumed_ticks") == 5,
          "Replay exhaustion counted neutral ticks as recorded samples");
    RuntimeInput canceled;
    canceled.configure(map());
    canceled.replay_input_recording(reopened, "source-a", 120);
    canceled.latch(1);
    canceled.latch(2);
    canceled.finish_input_recording();
    check(canceled.latch(3).actions.at(action).released &&
              !canceled.latch(4).actions.at(action).held,
          "Canceled replay continued recorded input");
    check(canceled.input_recording_status().at("consumed_ticks") == 2,
          "Canceled replay counted unconsumed samples");
    for (const auto& invalid : {Json(true), Json(1.5), Json(-1), Json(8)}) {
        bad = reopened;
        bad["frames"][0][0][2] = invalid;
        rejects([&] { validate_input_recording(bad); });
    }
    bad = reopened;
    bad["frames"][0][1][0] = std::numeric_limits<double>::infinity();
    rejects([&] { validate_input_recording(bad); });
    bad = reopened;
    bad["frames"][0][1][0] = 1e300;
    rejects([&] { validate_input_recording(bad); });
    for (const auto& version : {Json(true), Json(1.0), Json(2)}) {
        bad = reopened;
        bad["version"] = version;
        rejects([&] { validate_input_recording(bad); });
    }
    bad = reopened;
    bad["frames"][0].push_back(Json::array({0, 0, 0}));
    rejects([&] { validate_input_recording(bad); });
    bad = reopened;
    bad["frames"] = Json::array();
    rejects([&] { validate_input_recording(bad); });
    RuntimeInput full;
    full.configure(map());
    full.begin_input_recording("source-a", 60);
    for (std::size_t tick = 1; tick <= input_recording_tick_limit + 2; ++tick)
        full.latch(tick);
    check(full.input_recording_status().at("mode") == "recorded" &&
              full.input_recording_document().at("frames").size() == input_recording_tick_limit,
          "Recording limit was not bounded");
    validate_input_recording(full.input_recording_document());
    Json large{{"version", 1}, {"actions", Json::array()}};
    for (unsigned i = 0; i < 64; ++i)
        large["actions"].push_back(
            {{"id", ActionId::generate()},
             {"name", "Motion " + std::to_string(i)},
             {"kind", "axis2"},
             {"bindings", Json::array({{{"control", "mouse.delta_x"}, {"x", 100}, {"y", 0}},
                                       {{"control", "mouse.delta_y"}, {"x", 0}, {"y", 100}}})}});
    RuntimeInput bounded;
    bounded.configure(InputMap(large));
    bounded.begin_input_recording("source-a", 60);
    for (std::size_t tick = 1; tick <= input_recording_tick_limit; ++tick) {
        bounded.submit(
            {{"mouse.delta_x", 987654.3210987654}, {"mouse.delta_y", -987654.3210987654}});
        bounded.latch(tick);
        if (bounded.input_recording_status().at("mode") == "recorded")
            break;
    }
    check(bounded.input_recording_status().at("notice") == "Recording byte limit reached" &&
              bounded.input_recording_document().dump().size() <= input_recording_byte_limit &&
              bounded.input_recording_document().at("frames").size() < input_recording_tick_limit,
          "Recording byte bound did not retain a bounded complete prefix");
    validate_input_recording(bounded.input_recording_document());
    auto padded_map = map().source();
    padded_map["padding"] = "";
    RuntimeInput sizing;
    sizing.configure(InputMap(padded_map));
    sizing.begin_input_recording("source-a", 60);
    const auto header_bytes = sizing.input_recording_document().dump().size();
    padded_map["padding"] = std::string(input_recording_byte_limit - header_bytes - 8, 'x');
    RuntimeInput no_frame;
    no_frame.configure(InputMap(padded_map));
    no_frame.begin_input_recording("source-a", 60);
    no_frame.latch(1);
    check(no_frame.input_recording_status().at("mode") == "failed" &&
              no_frame.input_recording_status().at("ticks") == 0,
          "An input map leaving no frame budget was reported as a completed recording");
    RuntimeInput changed;
    changed.configure(map());
    changed.begin_input_recording("source-a", 60);
    changed.latch(1);
    changed.replace_map(map());
    check(changed.input_recording_status().at("mode") == "failed",
          "Map replacement silently changed a recording");
}
} // namespace
int main() {
    try {
        exercise();
        std::cout << "Fixed gameplay input recording, validation and replay passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
