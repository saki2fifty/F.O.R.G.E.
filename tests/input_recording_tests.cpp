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
void session_recordings() {
    const auto scene_a = AssetId::generate(), scene_b = AssetId::generate();
    RuntimeInput a;
    a.configure(map());
    rejects([&] { a.begin_session_input_recording("source-a", 120, {}); });
    a.begin_session_input_recording("source-a", 120, scene_a);
    a.submit({{"key.space", 1}, {"mouse.delta_x", 4}});
    const auto a1 = cells(a.latch(1));
    const auto a2 = cells(a.latch(2));
    const auto prefix = a.input_recording_document();
    RuntimeInput b;
    b.configure(map());
    b.inherit_session_input_recording(a, "source-b", 120, scene_b);
    check(a.input_recording_document() == prefix, "Candidate preparation modified the old take");
    b.submit({{"mouse.delta_x", -6}});
    const auto b1 = cells(b.latch(1));
    RuntimeInput again;
    again.configure(map());
    again.inherit_session_input_recording(b, "source-a", 120, scene_a);
    again.submit({{"key.space", 1}});
    const auto a3 = cells(again.latch(1));
    again.finish_input_recording();
    const auto take = again.input_recording_document();
    validate_input_recording(take);
    check(take.at("segments").size() == 3 &&
              again.input_recording_status().at("ticks").get<std::size_t>() == 4,
          "Session take lost total frames or repeated Scene visits");
    RuntimeInput replay_a;
    replay_a.configure(map());
    rejects([&] { replay_a.replay_input_recording(take, "source-a", 120, scene_b); });
    replay_a.replay_input_recording(take, "source-a", 120, scene_a);
    RuntimeInput replay_b;
    replay_b.configure(map());
    rejects([&] { replay_b.inherit_session_input_recording(replay_a, "source-b", 120, scene_b); });
    check(replay_a.input_recording_delays_scene(), "Replay allowed publication before its frames");
    check(cells(replay_a.latch(1)) == a1 && cells(replay_a.latch(2)) == a2,
          "First Scene replay changed captured actions");
    check(replay_a.input_recording_waits_for_scene(), "Replay did not wait at the Scene boundary");
    rejects([&] { replay_a.latch(3); });
    rejects([&] { replay_b.inherit_session_input_recording(replay_a, "wrong", 120, scene_b); });
    rejects([&] { replay_b.inherit_session_input_recording(replay_a, "source-b", 120, scene_a); });
    check(replay_b.input_recording_status().at("mode").get<std::string>() == "idle" &&
              replay_a.input_recording_status().at("consumed_ticks").get<std::size_t>() == 2,
          "Failed Scene admission changed either recording owner");
    replay_b.inherit_session_input_recording(replay_a, "source-b", 120, scene_b);
    check(cells(replay_b.latch(1)) == b1 && replay_b.input_recording_waits_for_scene(),
          "Second Scene replay lost actions or boundary");
    RuntimeInput replay_again;
    replay_again.configure(map());
    replay_again.inherit_session_input_recording(replay_b, "source-a", 120, scene_a);
    check(cells(replay_again.latch(1)) == a3 &&
              replay_again.input_recording_status().at("mode").get<std::string>() == "finished" &&
              replay_again.input_recording_status().at("consumed_ticks").get<std::size_t>() == 4,
          "Repeated Scene replay did not finish at the total frame count");
    const auto jump = map().actions().front().id;
    replay_again.submit({{"key.space", 1}});
    check(replay_again.latch(2).actions.at(jump).released &&
              !replay_again.latch(3).actions.at(jump).released,
          "Finished session replay did not neutralize the final held action once");
    rejects([&] { replay_again.replace_map(map()); });
    // A Scene may activate without running a fixed tick. Do not invent a frame.
    RuntimeInput zero;
    zero.configure(map());
    zero.begin_session_input_recording("source-a", 60, scene_a);
    RuntimeInput after_zero;
    after_zero.configure(map());
    after_zero.inherit_session_input_recording(zero, "source-b", 60, scene_b);
    after_zero.latch(1);
    after_zero.finish_input_recording();
    const auto zero_take = after_zero.input_recording_document();
    validate_input_recording(zero_take);
    RuntimeInput zero_replay;
    zero_replay.configure(map());
    zero_replay.replay_input_recording(zero_take, "source-a", 60, scene_a);
    check(zero_replay.input_recording_waits_for_scene(), "Tick-zero transition was not retained");
    rejects([&] { zero_replay.latch(1); });
    zero_replay.finish_input_recording();
    check(!zero_replay.input_recording_waits_for_scene(), "Cancel retained the Scene wait");
    zero_replay.submit({{"key.space", 1}});
    check(!zero_replay.latch(1).actions.at(jump).held, "Canceled session admitted physical input");
    for (const auto& key : {"scene", "source", "frames"}) {
        auto malformed = take;
        malformed["segments"][1].erase(key);
        rejects([&] { validate_input_recording(malformed); });
    }
    auto malformed = take;
    malformed["segments"][0]["source"] = "wrong";
    rejects([&] { validate_input_recording(malformed); });
    malformed = take;
    malformed["version"] = 2.0;
    rejects([&] { validate_input_recording(malformed); });
    malformed = take;
    malformed["segments"][1]["scene"] = "00000000-0000-0000-0000-000000000000";
    rejects([&] { validate_input_recording(malformed); });
    RuntimeInput bounded;
    bounded.configure(map());
    bounded.begin_session_input_recording("source-a", 60, scene_a);
    bounded.latch(1);
    for (std::size_t i = 0; i < input_recording_scene_limit; ++i) {
        RuntimeInput next;
        next.configure(map());
        next.inherit_session_input_recording(bounded, "source-b", 60, scene_b);
        bounded = std::move(next);
    }
    check(bounded.input_recording_status().at("mode").get<std::string>() == "recorded" &&
              bounded.input_recording_document().at("segments").size() ==
                  input_recording_scene_limit,
          "Scene capacity did not retain a bounded complete prefix");
    validate_input_recording(bounded.input_recording_document());
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
        session_recordings();
        exercise();
        std::cout << "Fixed gameplay input recording, validation and replay passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
