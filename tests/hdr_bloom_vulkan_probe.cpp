#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "hdr_bloom_gpu_tests.hpp"
#include "hdr_bloom_shaders.hpp"
#include "render_backend.hpp"
#include <fstream>
using namespace Diligent;
namespace {
const std::array<const char*, 4> names{"vertex", "extract", "blur", "composite"};
void emit(const std::filesystem::path& root) {
    const char* sources[]{forge::bloom_shader::vertex, forge::bloom_shader::extract,
                          forge::bloom_shader::blur, forge::bloom_shader::composite};
    for (unsigned i = 0; i < names.size(); ++i) {
        std::ofstream out(root / (std::string(names[i]) + ".hlsl"));
        if (i)
            out << forge::bloom_shader::common;
        out << sources[i];
        forge::test_bloom::check(bool(out.flush()), "Bloom shader source write failed");
    }
}
void execute(const std::filesystem::path& root) {
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo info;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
    forge::test_bloom::check(device && context, "Bloom Vulkan device creation failed");
    std::cout << "Bloom native adapter: " << device->GetAdapterInfo().Description << '\n';
    forge::HdrBloom bloom(
        device,
        [&](const ShaderCreateInfo& input, IShader** out) {
            const auto text = std::string(input.Desc.Name);
            unsigned index = 0;
            if (text.ends_with("extract"))
                index = 1;
            else if (text.ends_with("blur"))
                index = 2;
            else if (text.ends_with("composite"))
                index = 3;
            else
                forge::test_bloom::check(text.ends_with("vertex"), "Unexpected Bloom shader");
            std::ifstream file(root / (std::string(names[index]) + ".spv"), std::ios::binary);
            std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
            forge::test_bloom::check(!bytes.empty() && bytes.size() % 4 == 0,
                                     "Bloom SPIR-V input missing");
            auto ci = input;
            ci.Source = nullptr;
            ci.ByteCode = bytes.data();
            ci.ByteCodeSize = bytes.size();
            forge::prepare_renderer_shader(device->GetDeviceInfo(), ci);
            device->CreateShader(ci, out);
        },
        [&](const auto& ci, auto** out) { device->CreateGraphicsPipelineState(ci, out); }, 4096);
    forge::test_bloom::exercise(device, context, bloom, 4096);
}
} // namespace
int main(int argc, char** argv) {
    try {
        forge::test_bloom::check(argc == 3, "Usage: bloom probe emit|draw directory");
        if (std::string_view(argv[1]) == "emit")
            emit(argv[2]);
        else if (std::string_view(argv[1]) == "draw")
            execute(argv[2]);
        else
            throw std::runtime_error("Unknown bloom probe operation");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
