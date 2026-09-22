#pragma once
#include "diagnostic_source.hpp"
#include <climits>
#include <fstream>
void require(bool condition, const char* message);
inline void test_diagnostic_source() {
    using namespace forge;
    const auto root =
        std::filesystem::current_path() / ("source-navigation-" + AssetId::generate().str());
    std::filesystem::create_directories(root / "Shaders/lib");
    const auto write = [&](const char* name, const std::string& text) {
        std::ofstream file(root / name, std::ios::binary);
        require(bool(file.write(text.data(), std::streamsize(text.size()))),
                "Diagnostic source fixture could not write");
    };
    write("Shaders/lib/common.hlsli", "first line\nsecond line\n");
    ui::Problem problem{"shader",
                        "Error",
                        "lib/common.hlsli(1,1): warning X1: warning\n"
                        "lib/common.hlsli(2,3-6): error X3004: undeclared name\n",
                        {},
                        {},
                        {},
                        AssetId::generate()};
    require(ui::shader_diagnostic_location(problem, root, "Shaders") && problem.line == 2 &&
                problem.column == 3 && problem.source == "Shaders/lib/common.hlsli" &&
                problem.source_navigation,
            "Shader diagnostics lost their source root or selected a warning instead of an error");
    ui::DiagnosticSourceViewer viewer;
    viewer.open(root, problem);
    require(viewer.source() == problem.source && viewer.requested_offset() == 13,
            "Source viewer did not target the reported line/column");
    for (const char* message :
         {"../../escape.hlsl(1,1): error X1: reject", "/outside.hlsl(1,1): error X1: reject",
          "lib/common.hlsli(999999999999999,1): error X1: reject",
          "lib/common.hlsli(-1,1): error X1: reject", "lib/common.hlsli(1,-1): error X1: reject"}) {
        auto bad = problem;
        bad.text = message;
        require(!ui::shader_diagnostic_location(bad, root, "Shaders") &&
                    bad.source == problem.source,
                "Unsafe or malformed compiler coordinates replaced the safe diagnostic location");
    }
    write("binary.hlsl", std::string("bad\0source", 10));
    write("large.hlsl", std::string(1024 * 1024 + 1, 'x'));
    for (const char* source : {"../escape.hlsl", "binary.hlsl", "large.hlsl", "image.png"}) {
        auto bad = problem;
        bad.source = source;
        bool rejected = false;
        try {
            viewer.open(root, bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && viewer.source() == problem.source,
                "Unsafe source navigation replaced the previous read-only document");
    }
    problem.line = INT_MAX;
    problem.column = INT_MAX;
    viewer.open(root, problem);
    require(viewer.requested_offset() == 23, "Out-of-range source coordinates were not clamped");
    std::filesystem::remove_all(root);
}
