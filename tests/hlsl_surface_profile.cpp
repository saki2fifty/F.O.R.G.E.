// Focused Windows compiler regression; no engine/dependency build required.
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <wrl/client.h>
int wmain(int argc, wchar_t** argv) {
    if (argc < 2)
        return 2;
    bool passed = true;
    std::vector<std::string> entries{"zero_normal", "zero_basis", "dynamic_frame",
                                     "special_values"};
    if (argc > 2) {
        entries.clear();
        for (int i = 2; i < argc; ++i) {
            const std::wstring value = argv[i];
            entries.emplace_back(value.begin(), value.end());
        }
    }
    for (const auto& entry : entries) {
        for (bool optimized : {false, true}) {
            Microsoft::WRL::ComPtr<ID3DBlob> code, messages;
            const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
                               (optimized ? D3DCOMPILE_OPTIMIZATION_LEVEL3
                                          : D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG);
            const auto probe_define = std::string("FORGE_PROBE_") + entry;
            const D3D_SHADER_MACRO macros[] = {{probe_define.c_str(), "1"}, {nullptr, nullptr}};
            const auto result =
                D3DCompileFromFile(argv[1], macros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                   entry.c_str(), "ps_5_1", flags, 0, &code, &messages);
            const auto name = std::string(entry) + (optimized ? "-optimized" : "-debug");
            std::cout << name << ": " << (SUCCEEDED(result) ? "PASS" : "FAIL") << '\n';
            if (messages) {
                std::ofstream log(name + ".log", std::ios::binary);
                log.write(static_cast<const char*>(messages->GetBufferPointer()),
                          static_cast<std::streamsize>(messages->GetBufferSize()));
            }
            passed = passed && SUCCEEDED(result);
        }
    }
    return passed ? 0 : 1;
}
