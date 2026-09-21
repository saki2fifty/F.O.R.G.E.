// Standalone Windows SDK compiler probe. No engine build or dependency download.
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <string>
#include <wrl/client.h>
int main() {
    using Microsoft::WRL::ComPtr;
    bool supported = false;
    for (const std::string mode : {"scalar", "spaces", "array", "mixed"}) {
        for (const bool unbounded : {false, true}) {
            std::string source = "Texture2D<float4> texture0;\n";
            if (mode == "mixed")
                source += "SamplerState sampler0[17] : register(s0,space1);\nSamplerState "
                          "sampler17,sampler18;\n";
            else if (mode == "array")
                source += "SamplerState sampler0[19] : register(s0);\n";
            else
                for (unsigned i = 0; i < 19; ++i)
                    source +=
                        "SamplerState sampler" + std::to_string(i) +
                        (mode == "spaces" ? " : register(s0,space" + std::to_string(i) + ")" : "") +
                        ";\n";
            source += "float4 main(float2 uv:TEXCOORD0):SV_Target0 {float4 value=0;\n";
            for (unsigned i = 0; i < 19; ++i)
                source += "value+=texture0.Sample(" +
                          ((mode == "array" || (mode == "mixed" && i < 17))
                               ? "sampler0[" + std::to_string(i) + "]"
                               : "sampler" + std::to_string(i)) +
                          ",uv)/19;\n";
            source += "return value;}\n";
            const auto name = mode + (unbounded ? "-unbounded" : "-default");
            std::ofstream(name + ".hlsl") << source;
            ComPtr<ID3DBlob> code, messages;
            const unsigned flags = D3DCOMPILE_ENABLE_STRICTNESS |
                                   (unbounded ? D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES : 0);
            const auto result = D3DCompile(source.data(), source.size(), name.c_str(), nullptr,
                                           nullptr, "main", "ps_5_1", flags, 0, &code, &messages);
            std::cout << name << ": " << (SUCCEEDED(result) ? "PASS" : "FAIL") << '\n';
            if (messages) {
                std::ofstream log(name + ".log", std::ios::binary);
                log.write(static_cast<const char*>(messages->GetBufferPointer()),
                          static_cast<std::streamsize>(messages->GetBufferSize()));
            }
            if (SUCCEEDED(result)) {
                supported = true;
                std::ofstream output(name + ".dxbc", std::ios::binary);
                output.write(static_cast<const char*>(code->GetBufferPointer()),
                             static_cast<std::streamsize>(code->GetBufferSize()));
            }
        }
    }
    return supported ? 0 : 1;
}
