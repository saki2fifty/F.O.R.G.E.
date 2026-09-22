#include <forge/shader_asset.hpp>
#include <iostream>
using namespace forge;
using Json = nlohmann::json;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejected(F&& f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception&) {
        bad = true;
    }
    require(bad, "Invalid shader candidate was admitted");
}
Json reflection(ShaderStage stage) {
    return {
        {"stage", shader_stage_name(stage)},
        {"threads", stage == ShaderStage::Compute ? Json{8, 8, 1} : Json{0, 0, 0}},
        {"resources", Json::array({{{"name", "Material"},
                                    {"kind", "constant_buffer"},
                                    {"register", 2},
                                    {"space", 1},
                                    {"array_size", 1},
                                    {"dimension", "unknown"},
                                    {"size", 16},
                                    {"variables", Json::array({{{"name", "Color"},
                                                                {"class", "vector"},
                                                                {"basic", "float"},
                                                                {"rows", 1},
                                                                {"columns", 4},
                                                                {"offset", 0},
                                                                {"array_size", 0},
                                                                {"members", Json::array()}}})}}})}};
}
} // namespace
int main() {
    try {
        ShaderProgramSource program{{{ShaderStage::Vertex, "shaders/main.hlsl", "vs"},
                                     {ShaderStage::Pixel, "shaders/main.hlsl", "ps"}},
                                    {{"FIXED", "1"}},
                                    {{"SKIN", {"0", "1"}}}};
        ShaderSources sources{{"shaders/main.hlsl", "#include \"common.hlsli\"\n"},
                              {"shaders/common.hlsli", "// source bytes\n"}};
        const auto compiler = std::string(64, 'a');
        const auto build = shader_build_input(program, sources, {{"SKIN", "0"}}, compiler, false);
        const auto key = build.key();
        require(key == shader_build_input(program, sources, {{"SKIN", "0"}}, compiler, false).key(),
                "Shader key is nondeterministic");
        require(key != shader_build_input(program, sources, {{"SKIN", "1"}}, compiler, false).key(),
                "Permutation missing from shader key");
        require(key != shader_build_input(program, sources, {{"SKIN", "0"}}, std::string(64, 'b'),
                                          false)
                           .key(),
                "Compiler missing from shader key");
        require(key != shader_build_input(program, sources, {{"SKIN", "0"}}, compiler, true).key(),
                "Compiler debug flag missing from shader key");
        auto changed = sources;
        changed.at("shaders/common.hlsli") += "// changed\n";
        require(key != shader_build_input(program, changed, {{"SKIN", "0"}}, compiler, false).key(),
                "Include snapshot missing from shader key");
        rejected([&] { select_shader_permutation(program, {}); });
        rejected([&] { select_shader_permutation(program, {{"SKIN", "2"}}); });
        auto bad_program = program;
        bad_program.stages.push_back(program.stages.front());
        rejected([&] { validate_shader_program(bad_program); });
        bad_program = program;
        bad_program.stages.push_back({ShaderStage::Compute, "compute.hlsl"});
        rejected([&] { validate_shader_program(bad_program); });
        bad_program = program;
        bad_program.stages.push_back({ShaderStage::Hull, "tess.hlsl"});
        rejected([&] { validate_shader_program(bad_program); });
        bad_program = program;
        bad_program.stages.front().source = "../escape.hlsl";
        rejected([&] { validate_shader_program(bad_program); });
        changed = sources;
        changed["shaders/Main.hlsl"] = "";
        rejected([&] { validate_shader_sources(changed); });
        changed = sources;
        changed.begin()->second = std::string("a\0b", 3);
        rejected([&] { validate_shader_sources(changed); });
        changed = sources;
        changed.begin()->second = std::string(2 * 1024 * 1024 + 1, ' ');
        rejected([&] { validate_shader_sources(changed); });
        Json doc{{"format", "forge.shader"},
                 {"version", 1},
                 {"asset_id", AssetId::generate()},
                 {"stages", Json::array({{{"stage", "vertex"}, {"source", "main.hlsl"}}})},
                 {"unknown.plugin", {{"retain", 42}}}};
        const auto original = doc;
        const auto projection = shader_program_source(doc);
        require(doc == original && projection.stages[0].entry == "main",
                "Projection rewrote authored shader source");
        doc["stages"][0]["stage"] = "mesh";
        rejected([&] { shader_program_source(doc); });
        auto r = reflection(ShaderStage::Vertex);
        validate_shader_reflection(r);
        auto b = r;
        b["resources"][0]["variables"][0]["offset"] = 16;
        rejected([&] { validate_shader_reflection(b); });
        b = r;
        b["resources"].push_back(b["resources"][0]);
        b["resources"][1]["name"] = "Other";
        rejected([&] { validate_shader_reflection(b); });
        b = r;
        b["resources"][0]["array_size"] = 0;
        rejected([&] { validate_shader_reflection(b); });
        b = r;
        b["resources"][0]["variables"][0]["columns"] = 5;
        rejected([&] { validate_shader_reflection(b); });
        b = reflection(ShaderStage::Compute);
        b["threads"] = {1024, 2, 1};
        rejected([&] { validate_shader_reflection(b); });
        // Envelope/data admission is independent from native DXBC validation.
        // These opaque bytes are never supplied to a graphics API in this test.
        ShaderData data{key,
                        compiler,
                        true,
                        false,
                        {{ShaderStage::Vertex, "main", {std::byte{1}, std::byte{2}}, r}}};
        const auto bytes = encode_shader(data);
        const auto loaded = decode_shader(bytes);
        require(encode_shader(loaded) == bytes && loaded.layout_digest() == data.layout_digest(),
                "Shader artifact roundtrip changed data");
        {
            SurfaceShaderDefinition def;
            def.parameters["tint"] = {MaterialParameterType::LinearColor4, {1, .5f, .25f, 1}};
            auto surface_doc = original;
            surface_doc["stages"] =
                Json::array({{{"stage", "pixel"}, {"source", "main.hlsl"}, {"entry", "Shade"}}});
            surface_doc["surface"] = surface_definition_document(def);
            rejected([&] { shader_program_source(surface_doc); });
            surface_doc["version"] = 2;
            const auto surface_program = shader_program_source(surface_doc);
            require(surface_program.surface == def &&
                        surface_doc.at("unknown.plugin") == original.at("unknown.plugin"),
                    "Surface source lost declaration/unknown fields");
            const ShaderSources code{
                {"main.hlsl", "float4 Shade(ForgeSurfaceInput v){return ForgeParameter_tint();}"}};
            const auto input = shader_build_input(surface_program, code, {}, compiler, false);
            auto changed_surface = surface_program;
            changed_surface.surface->parameters["tint"].value[0] = .1f;
            require(input.key() !=
                        shader_build_input(changed_surface, code, {}, compiler, false).key(),
                    "Surface declaration omitted from shader build identity");
            ShaderData surface_data;
            surface_data.build_key = input.key();
            surface_data.compiler_digest = compiler;
            surface_data.surface = def;
            auto empty = reflection(ShaderStage::Pixel);
            empty["resources"] = Json::array();
            surface_data.stages = {{ShaderStage::Pixel,
                                    "ForgeSurfaceColor",
                                    {std::byte{1}},
                                    empty,
                                    ShaderEntryRole::SurfaceColor},
                                   {ShaderStage::Pixel,
                                    "ForgeSurfaceDepth",
                                    {std::byte{2}},
                                    empty,
                                    ShaderEntryRole::SurfaceDepth}};
            const auto encoded_surface = encode_shader(surface_data);
            require(std::to_integer<unsigned>(encoded_surface[8]) == 2 &&
                        encode_shader(decode_shader(encoded_surface)) == encoded_surface &&
                        std::to_integer<unsigned>(bytes[8]) == 1,
                    "Surface envelope version/roundtrip is incorrect or changed legacy data");
            auto bad_surface = surface_data;
            bad_surface.stages.pop_back();
            rejected([&] { validate_shader(bad_surface); });
            bad_surface = surface_data;
            bad_surface.stages[1].role = ShaderEntryRole::SurfaceColor;
            rejected([&] { validate_shader(bad_surface); });
            bad_surface = surface_data;
            bad_surface.surface.reset();
            rejected([&] { validate_shader(bad_surface); });
            auto old_reader_spelling = encoded_surface;
            old_reader_spelling[8] = std::byte{1};
            rejected([&] { decode_shader(old_reader_spelling); });
        }
        auto changed_data = data;
        changed_data.stages[0].reflection["resources"][0]["register"] = 3;
        require(changed_data.layout_digest() != data.layout_digest(),
                "Register omitted from layout digest");
        changed_data = data;
        changed_data.stages[0].bytecode.push_back(std::byte{3});
        require(changed_data.layout_digest() == data.layout_digest(),
                "Bytecode contents falsely changed binding layout");
        for (std::size_t length : {std::size_t{0}, std::size_t{23}, bytes.size() - 1})
            rejected([&] { decode_shader(std::span(bytes).first(length)); });
        auto broken = bytes;
        broken.back() = std::byte{0};
        rejected([&] { decode_shader(broken); });
        require(data.resident_bytes() >= bytes.size(),
                "Shader resident estimate missed stored data");
        std::cout << "Shader source/permutation/reflection/artifact tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
