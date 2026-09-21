#include "asset_watch.hpp"
#include <algorithm>
#include <forge/asset_discovery.hpp>
#include <forge/identity.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid source operation accepted");
}
void write(const std::filesystem::path& file, std::string_view contents) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    output << contents;
    output.close();
    require(bool(output), "Fixture write failed");
}
bool has_code(const SourceSnapshot& snapshot, const char* code) {
    return std::any_of(snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                       [&](const auto& item) { return item.code == code; });
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        const auto scratch = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
            }
        } cleanup{scratch};
        const auto project = scratch / "project";
        std::filesystem::create_directories(project);
        require(scan_asset_sources(project).files.empty(), "Missing Assets is not empty");
        write(project / "Assets/A.PNG", "first");
        write(project / "Assets/nested/Model.glb", "not yet parsed");
        write(project / "Assets/z.unknown", "unknown source");
        for (const auto* name :
             {".hidden/a.png", "a.tmp", "b.pending", "b.swp", "b.bak", "~lock", "a~"})
            write(project / "Assets" / name, "filtered");
        auto initial = scan_asset_sources(project);
        require(initial.complete && initial.files.size() == 3 && initial.filtered == 7,
                "Initial discovery/filtering incorrect");
        require(initial.files.at("Assets/A.PNG").source_kind == "image" &&
                    initial.files.at("Assets/nested/Model.glb").source_kind == "model" &&
                    initial.files.at("Assets/z.unknown").source_kind == "unrecognized",
                "Source recognition was case sensitive or hid unknown inputs");
        require(initial.files.at("Assets/A.PNG").digest ==
                    "a7937b64b8caa58f03721bb6bacf5c78cb235febe0e70b1b84cd99541461a08e",
                "Incorrect source content digest");
#ifdef _WIN32
        const ProjectLocatorLess less;
        require(!less("Assets/A.PNG", "assets\\a.png") && !less("assets\\a.png", "Assets/A.PNG"),
                "Equivalent Windows separators split one source locator");
        require(std::filesystem::exists(project / "assets/a.png") &&
                    initial.files.contains("assets/a.png") &&
                    initial.files.contains("assets\\a.png"),
                "Source lookup disagrees with Windows project locator case semantics");
        {
            SourceChangeTracker own(initial, 0ms);
            auto replacement = initial;
            const std::string digest(64, 'f');
            replacement.files.at("Assets/A.PNG").digest = digest;
            own.acknowledge_write("assets/a.png", digest);
            const auto now = SourceChangeTracker::Clock::now();
            own.observe(std::move(replacement), now);
            require(own.drain(now).empty(),
                    "Case-equivalent self-write triggered a redundant Windows reimport");
        }
#else
        require(!initial.files.contains("assets/a.png"),
                "POSIX source lookup incorrectly folded case");
#endif
        auto repeated = scan_asset_sources(project);
        require(repeated.files.begin()->first == initial.files.begin()->first &&
                    repeated.files.at("Assets/A.PNG").digest ==
                        initial.files.at("Assets/A.PNG").digest,
                "Scan ordering/digest changed without a write");
        SourceScanOptions options;
        options.roots = {"Assets/nested", "Assets"};
        auto overlapping = scan_asset_sources(project, options);
        require(overlapping.complete && overlapping.files.size() == 3,
                "Overlapping roots produced duplicate sources");
        options = {};
        options.ignored = {"Assets/nested"};
        require(scan_asset_sources(project, options).files.size() == 2, "Directory ignore failed");
        options.max_files = 1;
        require(!scan_asset_sources(project, options).complete, "File count was not bounded");
        options = {};
        options.max_file_bytes = 4;
        require(has_code(scan_asset_sources(project, options), "byte_limit"),
                "File byte bound ignored");
        options = {};
        options.max_total_bytes = 6;
        auto limited = scan_asset_sources(project, options);
        require(!limited.complete && limited.bytes_read <= 6, "Aggregate scan byte bound ignored");
        options = {};
        options.max_depth = 1;
        require(!scan_asset_sources(project, options).complete, "Directory depth bound ignored");
        options.roots = {"../outside"};
        rejects([&] { (void)scan_asset_sources(project, options); });
        std::stop_source stop;
        stop.request_stop();
        require(has_code(scan_asset_sources(project, {}, stop.get_token()), "cancelled"),
                "Scan ignored cancellation");

        const auto now = SourceChangeTracker::Clock::now();
        SourceChangeTracker tracker(initial);
        const auto stamp = std::filesystem::last_write_time(project / "Assets/A.PNG");
        write(project / "Assets/A.PNG", "other"); // Same byte count AND restored mtime.
        std::filesystem::last_write_time(project / "Assets/A.PNG", stamp);
        auto modified = scan_asset_sources(project);
        tracker.observe(modified, now);
        require(tracker.generation() == 2 && tracker.drain(now + 199ms).empty(),
                "Change generation/debounce incorrect");
        tracker.observe(modified, now + 150ms); // Duplicate event must not prolong debounce.
        auto events = tracker.drain(now + 200ms);
        require(events.size() == 1 && events[0].kind == SourceChangeKind::Modified &&
                    events[0].generation == 2 && events[0].previous_digest != events[0].digest,
                "Same-size/mtime content change missed or duplicate event repeated");
        require(tracker.drain(now + 1s).empty(), "Event delivered twice");
        std::filesystem::rename(project / "Assets/A.PNG", project / "Assets/moved.png");
        auto moved = scan_asset_sources(project);
        tracker.observe(moved, now + 1s);
        events = tracker.drain(now + 1200ms);
        require(events.size() == 1 && events[0].kind == SourceChangeKind::Moved &&
                    events[0].source == "Assets/moved.png" &&
                    events[0].previous_source == "Assets/A.PNG",
                "Rename evidence lost source identity");

        // A copy has equal bytes but is a NEW logical source; never infer a rename.
        std::filesystem::copy_file(project / "Assets/moved.png", project / "Assets/copy.png");
        std::filesystem::remove(project / "Assets/moved.png");
        tracker.observe(scan_asset_sources(project), now + 2s);
        events = tracker.drain(now + 2200ms);
        require(events.size() == 2 && events[0].kind == SourceChangeKind::Created &&
                    events[1].kind == SourceChangeKind::Removed,
                "Copied bytes silently retargeted logical identity");

        // Failed/partial scans do not turn inaccessible sources into deletions.
        auto partial = scan_asset_sources(project);
        partial.files.clear();
        partial.complete = false;
        const auto generation = tracker.generation();
        tracker.observe(partial, now + 3s);
        require(!tracker.complete() && tracker.generation() > generation &&
                    tracker.drain(now + 4s).empty(),
                "Incomplete scan published deletions");
        tracker.observe(scan_asset_sources(project), now + 4s);
        require(tracker.complete() && tracker.drain(now + 4200ms).empty(),
                "Scan recovery invented changes");
        rejects([&] { SourceChangeTracker invalid(partial); });

        write(project / "Assets/copy.png", "authored write");
        auto authored = scan_asset_sources(project);
        tracker.acknowledge_write("Assets/copy.png", authored.files.at("Assets/copy.png").digest);
        tracker.observe(authored, now + 5s);
        require(tracker.drain(now + 5200ms).empty(), "Matching self-write was not coalesced");
        tracker.acknowledge_write("Assets/copy.png", authored.files.at("Assets/copy.png").digest);
        write(project / "Assets/copy.png", "external write");
        tracker.observe(scan_asset_sources(project), now + 6s);
        require(tracker.drain(now + 6200ms).size() == 1, "External write incorrectly suppressed");
        rejects([&] { tracker.acknowledge_write("Assets/copy.png", "not-a-digest"); });

        // An overlapping scan can observe an unrelated path before it sees our
        // committed write. Draining that path must not discard our receipt.
        {
            auto before = scan_asset_sources(project);
            SourceChangeTracker delayed(before, 0ms);
            auto after = before;
            after.files.at("Assets/copy.png").digest = authored.files.at("Assets/copy.png").digest;
            delayed.acknowledge_write("Assets/copy.png", after.files.at("Assets/copy.png").digest);
            auto unrelated = before.files.at("Assets/copy.png");
            unrelated.source = "Assets/unrelated.png";
            before.files.emplace("Assets/unrelated.png", unrelated);
            after.files.emplace("Assets/unrelated.png", unrelated);
            delayed.observe(before, now);
            require(delayed.drain(now).size() == 1,
                    "Unrelated change disappeared behind self-write receipt");
            delayed.observe(after, now + 1s);
            require(delayed.drain(now + 1s).empty(),
                    "Overlapping scan consumed an unobserved self-write receipt");
            delayed.observe(before, now + 2s);
            require(delayed.drain(now + 2s).size() == 1,
                    "Consumed self-write receipt hid a later external change");
        }

        // Atomic same-content replacement refreshes ephemeral identity evidence.
        write(project / "replacement", "external write");
        std::filesystem::remove(project / "Assets/copy.png");
        std::filesystem::rename(project / "replacement", project / "Assets/copy.png");
        tracker.observe(scan_asset_sources(project), now + 7s);
        require(tracker.drain(now + 7200ms).empty(), "Same-content save triggered rebuild");
        std::filesystem::rename(project / "Assets/copy.png", project / "Assets/again.png");
        tracker.observe(scan_asset_sources(project), now + 8s);
        events = tracker.drain(now + 8200ms);
        require(events.size() == 1 && events[0].kind == SourceChangeKind::Moved,
                "Same-content replacement left stale OS identity evidence");

        std::filesystem::create_hard_link(project / "Assets/again.png",
                                          project / "Assets/alias.png");
        auto aliases = scan_asset_sources(project);
        require(aliases.complete && has_code(aliases, "source_alias"),
                "Hardlink alias not diagnosed");
        SourceChangeTracker alias_tracker(aliases, 0ms);
        std::filesystem::rename(project / "Assets/again.png", project / "Assets/renamed-alias.png");
        alias_tracker.observe(scan_asset_sources(project), now);
        for (const auto& event : alias_tracker.drain(now))
            require(event.kind != SourceChangeKind::Moved,
                    "Ambiguous hardlink inferred unique move");

        write(scratch / "outside/private.png", "outside");
        std::error_code link_error;
        std::filesystem::create_directory_symlink(scratch / "outside", project / "Assets/escape",
                                                  link_error);
        if (!link_error) {
            const auto escaped = scan_asset_sources(project);
            require(!escaped.complete && !escaped.files.contains("Assets/escape/private.png"),
                    "Scanner read external symlink source");
            SourceScanOptions ignore_external;
            ignore_external.ignored = {"Assets/escape"};
            require(scan_asset_sources(project, ignore_external).complete,
                    "Explicitly ignored subtree was still resolved/read");
            std::filesystem::remove(project / "Assets/escape");
            std::filesystem::create_directory_symlink(project / "Assets", project / "Assets/loop");
            const auto loop = scan_asset_sources(project);
            require(loop.complete && has_code(loop, "directory_alias"),
                    "Directory alias loop not bounded");
        } else {
            std::cout << "Symlink fixture unavailable: " << link_error.message() << '\n';
        }
        {
            const auto watched = scratch / "watched";
            write(watched / "surface.hlsl", "first");
            write(watched / "Assets/image.png", "pixels");
            write(watched / ".forge/cache/ignored.bin", "derived");
            SourceScanOptions all;
            all.include_project_root = true;
            all.max_file_bytes = 64;
            const auto root_scan = scan_asset_sources(watched, all);
            require(root_scan.complete && root_scan.files.size() == 2 &&
                        root_scan.files.contains("surface.hlsl") &&
                        !root_scan.files.contains(".forge/cache/ignored.bin"),
                    "Project-wide traversal omitted root sources or admitted derived cache");
            rejects([&] { (void)ProjectPaths(watched).resolve("."); });
            AssetSourceWatch watch(watched, all, 10min, 0ms);
            const auto initial_generation = watch.generation();
            auto next = [&] {
                const auto deadline = AssetSourceWatch::Clock::now() + 5s;
                for (;;) {
                    if (auto update = watch.poll())
                        return *update;
                    require(AssetSourceWatch::Clock::now() < deadline, "Watch delivery stalled");
                    std::this_thread::sleep_for(1ms);
                }
            };
            require(!watch.complete(), "Watch reported complete before initial scan");
            auto update = next();
            require(watch.complete() && update.generation == initial_generation + 1 &&
                        update.changes.size() == 2,
                    "Watch did not deliver deterministic initial discovery");
            const auto old_stamp = std::filesystem::last_write_time(watched / "surface.hlsl");
            write(watched / "surface.hlsl", "other");
            std::filesystem::last_write_time(watched / "surface.hlsl", old_stamp);
            for (int i = 0; i < 20; ++i)
                watch.rescan();
            update = next();
            require(update.changes.size() == 1 &&
                        update.changes[0].kind == SourceChangeKind::Modified &&
                        update.generation == initial_generation + 2,
                    "Manual rescan did not coalesce or relied on timestamps");
            write(watched / "surface.hlsl", "saved");
            const auto saved = scan_asset_sources(watched, all);
            watch.acknowledge_write("surface.hlsl", saved.files.at("surface.hlsl").digest);
            watch.rescan();
            update = next();
            require(update.changes.empty(), "Exact self-write suppression repeated an edit");
            std::filesystem::rename(watched / "surface.hlsl", watched / "renamed.hlsl");
            watch.rescan();
            update = next();
            require(update.changes.size() == 1 &&
                        update.changes[0].kind == SourceChangeKind::Moved &&
                        update.changes[0].previous_source == "surface.hlsl" &&
                        update.changes[0].source == "renamed.hlsl",
                    "Watch discarded reliable source move evidence");
            std::filesystem::remove(watched / "renamed.hlsl");
            write(watched / "too-large.bin", std::string(65, 'x'));
            watch.rescan();
            update = next();
            require(!watch.complete() && !update.snapshot->complete && update.changes.empty(),
                    "Incomplete watch scan inferred deletion");
            std::filesystem::remove(watched / "too-large.bin");
            watch.rescan();
            update = next();
            require(watch.complete() && update.changes.size() == 1 &&
                        update.changes[0].kind == SourceChangeKind::Removed,
                    "Rescan did not recover the pending deletion after scan failure");
            auto off_thread = std::async(std::launch::async, [&] {
                rejects([&] { watch.rescan(); });
                rejects([&] { (void)watch.poll(); });
            });
            off_thread.get();
            watch.rescan();
            (void)watch.poll(); // Destruction cancels/joins an in-flight scan.
        }
        std::cout << "Source discovery and debounced change tracking passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
