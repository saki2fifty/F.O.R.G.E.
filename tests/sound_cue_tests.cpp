#include <forge/sound_cue.hpp>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& work) {
    bool failed = false;
    try {
        work();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid Sound Cue admitted");
}
EntityRef source(unsigned n) {
    return {AssetId::parse("11111111-1111-4111-8111-111111111111"),
            EntityId::parse(n == 1   ? "22222222-2222-4222-8222-222222222222"
                            : n == 2 ? "33333333-3333-4333-8333-333333333333"
                                     : "44444444-4444-4444-8444-444444444444")};
}
SoundCue example() {
    return {true,
            19,
            0,
            {{1, {}, {{1, 1}, {2, 1}}},
             {0, source(1), {}},
             {3, {}, {{3, 1}, {4, 1}}},
             {0, source(2), {}},
             {0, source(3), {}}}};
}
void selection() {
    auto cue = example();
    const auto program = SoundCueProgram::compile(cue);
    check(program.maximum_outputs() == 2, "Layer/choice maximum is wrong");
    SoundCueState state;
    for (unsigned i = 0; i < 100; ++i) {
        const auto before = state;
        const auto selected = program.select(state);
        check(state == before, "Selection mutated caller state before admission");
        check(selected.sources == std::vector<EntityRef>{source(1), source(i % 2 ? 3 : 2)},
              "Layer order or per-trigger ordered branch changed");
        check(selected.next.triggers == i + 1 && selected.next.visits[2] == i + 1,
              "Selection did not advance visited nodes exactly once");
        state = selected.next;
    }
    // A shared ordered node advances on each visit, including twice in one layer.
    cue.nodes[0].children = {{2, 1}, {2, 1}};
    auto selected = SoundCueProgram::compile(cue).select({});
    check(selected.sources == std::vector<EntityRef>{source(2), source(3)} &&
              selected.next.visits[2] == 2,
          "Shared-node visit ordering is ambiguous");
    cue.nodes[0] = {2, {}, {{1, 1}, {3, 3}, {4, 6}}};
    const auto weighted = SoundCueProgram::compile(cue);
    state = {};
    std::array<unsigned, 3> counts{};
    for (unsigned i = 0; i < 4000; ++i) {
        selected = weighted.select(state);
        check(selected.sources.size() == 1, "Weighted choice emitted more than one leaf");
        unsigned found = 0;
        while (found < 3 && selected.sources[0] != source(found + 1))
            ++found;
        check(found < 3, "Weighted choice selected an unknown leaf");
        ++counts[found];
        check(selected.sources == weighted.select(state).sources,
              "Same seed/state was not deterministic");
        state = selected.next;
    }
    check(counts[0] > 300 && counts[0] < 500 && counts[1] > 1050 && counts[1] < 1350 &&
              counts[2] > 2200 && counts[2] < 2600,
          "Weighted choice lost declared relative weights");
    auto changed_seed = cue;
    ++changed_seed.seed;
    check(weighted.select({}).sources !=
                  SoundCueProgram::compile(changed_seed).select({}).sources ||
              weighted.select(state).sources !=
                  SoundCueProgram::compile(changed_seed).select(state).sources,
          "Seed has no effect on weighted branch choices");
    state.visits[0] = std::numeric_limits<std::uint64_t>::max();
    reject([&] { (void)weighted.select(state); });
    state = {};
    state.triggers = std::numeric_limits<std::uint64_t>::max();
    reject([&] { (void)weighted.select(state); });
}
void admission() {
    auto cue = example();
    check(sound_cue_from_json(sound_cue_json(cue)) == cue, "Cue JSON roundtrip changed values");
    check(SoundCueProgram::compile({}).maximum_outputs() == 0, "Empty draft acquired outputs");
    reject([] { (void)SoundCueProgram::compile({}).select({}); });
    cue.enabled = false;
    reject([&] { (void)SoundCueProgram::compile(cue).select({}); });
    cue = example();
    cue.nodes.push_back({1, {}, {{5, 1}}});
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue = example();
    cue.nodes[4].children = {{1, 1}};
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue = example();
    cue.nodes[0].children[0].weight = 2;
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue = example();
    cue.nodes[0].children[0].target = 32;
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue = example();
    cue.nodes[1].source.scene = {};
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue = example();
    cue.nodes[1].source = {};
    reject([&] { (void)SoundCueProgram::compile(cue).select({}); });
    cue = example();
    cue.nodes.resize(33);
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue = {true,
           1,
           0,
           {{1, {}, {{1, 1}, {1, 1}}},
            {1, {}, {{2, 1}, {2, 1}, {2, 1}, {2, 1}, {2, 1}, {2, 1}, {2, 1}, {2, 1}}},
            {0, source(1), {}}}};
    check(SoundCueProgram::compile(cue).select({}).sources.size() == 16,
          "Exact layer limit rejected");
    cue.nodes[0].children.push_back({2, 1});
    reject([&] { (void)SoundCueProgram::compile(cue); });
    for (auto invalid : {Json(-1), Json(1.0), Json(true), Json(UINT64_MAX)}) {
        auto value = sound_cue_json(example());
        value["seed"] = invalid;
        reject([&] { (void)sound_cue_from_json(value); });
    }
    auto value = sound_cue_json(example());
    value["nodes"][0]["children"] = Json::object();
    reject([&] { (void)sound_cue_from_json(value); });
}
} // namespace
int main() {
    try {
        selection();
        admission();
        std::cout << "Sound Cue selection/admission passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
