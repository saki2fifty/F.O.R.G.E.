#pragma once
namespace forge {
inline constexpr const char* fullscreen_triangle_vertex = R"(
float4 main(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3:-1,id==1?3:-1,0,1);
})";
} // namespace forge
