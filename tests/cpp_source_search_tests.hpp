#pragma once
#include "cpp_source_search.hpp"
#include <thread>

void require(bool, const char*);
template <class F> void rejects(F, const char*);
inline void test_cpp_source_search(const std::filesystem::path& root) {
    using namespace forge;
    using namespace forge::ui;
    std::filesystem::create_directories(root / "Native/Components");
    atomic_write(
        root / "Native/Components/Rotator.hpp",
        "// café\r\n\tfloat speed = 90;\r\nSpeed speeds _speed speed_ caféSpeed Speedé\r\n");
    atomic_write(root / "Native/Spin.cpp", "speed speed\n");
    atomic_write(root / "Native/Builds/Hidden.cpp", "speed");
    atomic_write(root / "Native/.forge/Hidden.hpp", "speed");
    atomic_write(root / "Native/readme.txt", "speed");
    atomic_write(root / "External.cpp", "speed");
    auto found = search_cpp_sources(root, {"speed", true, true});
    require(found.complete() && found.files == 2 && found.matches.size() == 3,
            "Project search lost whole-word matches or searched generated/external files");
    const auto& first = found.matches.front();
    require(first.path == "Native/Components/Rotator.hpp" && first.line == 2 &&
                first.column == 11 && first.excerpt == "\tfloat speed = 90;" && first.length == 5 &&
                !first.draft,
            "Project search lost sorted paths, CRLF, UTF-8 or visual tab columns");
    found = search_cpp_sources(root, {"SPEED", false, true});
    require(found.complete() && found.matches.size() == 4,
            "ASCII insensitive search split a non-ASCII word or ignored case");
    found = search_cpp_sources(root, {"café", true, false});
    require(found.matches.size() == 2 && found.matches[1].line == 3 &&
                found.matches[1].column == 28,
            "Literal UTF-8 matching or code-point columns changed");
    CppSearchDrafts drafts{{"Native/Spin.cpp", "// unsaved speed\n"},
                           {"Native/Deleted.hpp", "speed\n"}};
    found = search_cpp_sources(root, {"speed", true, true}, drafts);
    require(found.complete() && found.files == 3 && found.matches.size() == 3 &&
                found.matches.back().draft && found.matches[1].draft &&
                read_cpp_source(root / "Native/Spin.cpp") == "speed speed\n",
            "Search did not snapshot drafts/deleted open files or changed disk");
    rejects([&] { validate_cpp_search({""}); }, "Empty project search accepted");
    rejects([&] { validate_cpp_search({"one\ntwo"}); }, "Multiline search accepted");
    rejects([&] { validate_cpp_search({std::string(256, 'a')}); }, "Oversized query accepted");
    rejects([&] { validate_cpp_search({std::string("\xff", 1)}); }, "Invalid query UTF-8 accepted");
    std::stop_source stopped;
    stopped.request_stop();
    found = search_cpp_sources(root, {"speed"}, {}, stopped.get_token());
    require(found.cancelled && !found.complete() && found.matches.empty(),
            "Cancelled source discovery claimed complete results");
    atomic_write(root / "Native/Bad.cpp", std::string("bad\0text", 8));
    found = search_cpp_sources(root, {"speed"});
    require(!found.complete() && found.skipped == 1 && found.errors.size() == 1 &&
                !found.matches.empty(),
            "Invalid file hid partial search status");
    std::filesystem::remove(root / "Native/Bad.cpp");
    std::string repeated;
    for (unsigned i = 0; i < 600; ++i)
        repeated += "needle ";
    atomic_write(root / "Native/Many.cpp", repeated);
    found = search_cpp_sources(root, {"needle"});
    require(found.limited && !found.complete() && found.matches.size() == cpp_search_match_limit,
            "Search result budget was silently exceeded or claimed complete");
    std::filesystem::remove(root / "Native/Many.cpp");
    const std::string crossing = std::string(4094, 'x') + "needle" + std::string(400, 'x');
    atomic_write(root / "Native/Crossing.cpp", crossing);
    found = search_cpp_sources(root, {"needle"});
    require(found.complete() && found.matches.size() == 1 && found.matches[0].offset == 4094,
            "Chunked cancellation boundaries lost a crossing match");
    const auto budget_root = root / "budget";
    std::filesystem::create_directories(budget_root / "Native");
    CppSearchDrafts large;
    for (unsigned i = 0; i < 33; ++i)
        large.emplace(std::filesystem::path("Native") / (std::to_string(i) + ".hpp"),
                      std::string(max_cpp_source_bytes, 'x'));
    found = search_cpp_sources(budget_root, {"absent"}, large);
    require(found.limited && !found.complete() && found.bytes == cpp_search_byte_limit &&
                found.files == 32,
            "Project search byte budget was ignored");
    CppSourceSearch task;
    rejects([&] { task.start(budget_root, {"absent"}, large); },
            "Unbounded source snapshot was scheduled");
    task.start(root, {"speed"}, {});
    rejects([&] { task.start(root, {"speed"}, {}); },
            "Overlapping source-search job was scheduled");
    task.cancel();
    std::optional<CppSearchResult> ready;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!(ready = task.poll()) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    require(ready && ready->cancelled && !ready->complete() && !task.busy(),
            "Source search cancellation/result lifetime failed");
#ifndef _WIN32
    std::filesystem::create_directory_symlink(root, root / "Native/Alias");
    std::filesystem::create_symlink(root / "External.cpp", root / "Native/Alias.cpp");
    found = search_cpp_sources(root, {"speed", true, true});
    require(found.complete() && found.files == 3 && found.matches.size() == 3,
            "Project search followed a symlink or recursively entered an alias");
#endif
}
