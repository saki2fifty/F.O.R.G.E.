#pragma once
#include "reflection_source_plan.hpp"
#include <string>
namespace forge {
inline constexpr unsigned reflection_source_radius_limit = 24;
static_assert((1u << reflection_source_level_limit) >= 2 * reflection_source_radius_limit);
static_assert((1u << (reflection_source_level_limit - 1)) < 2 * reflection_source_radius_limit);
inline std::string reflection_source_name(unsigned level, bool depth) {
    return std::string(depth ? "SourceRange" : "SourceColor") + std::to_string(level);
}
inline std::string reflection_source_lookup_shader() {
    std::string result = "#define FORGE_REFLECTION_SOURCE_LEVELS " +
                         std::to_string(reflection_source_level_limit) +
                         "\n#define FORGE_REFLECTION_SOURCE_RADIUS " +
                         std::to_string(reflection_source_radius_limit) + "\n";
    for (unsigned i = 0; i < reflection_source_level_limit; ++i)
        result += "Texture2D<float4> " + reflection_source_name(i, false) + ";Texture2D<float2> " +
                  reflection_source_name(i, true) + ";\n";
    result += R"(
void sourceCell(int level,int2 at,out float4 color,out float2 range) {
    int scale=1<<(level+1);int2 size=(int2(Extent.xy)+scale-1)/scale;
    color=0;range=0;if(any(at<0)||any(at>=size))return;
)";
    for (unsigned i = 0; i < reflection_source_level_limit; ++i)
        result += "if(level==" + std::to_string(i) + "){color=" + reflection_source_name(i, false) +
                  ".Load(int3(at,0));range=" + reflection_source_name(i, true) +
                  ".Load(int3(at,0));return;}\n";
    result += "}\n";
    result += R"(
bool sourceAt(int level,float2 uv,float3 hit,out float4 sum) {
    float scale=float(1<<(level+1));float2 at=uv*Extent.xy/scale-.5;
    int2 first=int2(floor(at));float2 f=frac(at);sum=0;float2 bounds=float2(3.402823e38,0);
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x) {
        float4 color;float2 range;sourceCell(level,first+int2(x,y),color,range);
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(color.a>0&&weight>0){sum+=color*weight;bounds=float2(min(bounds.x,range.x),max(bounds.y,range.y));}
    }
    return sum.a>=.01&&bounds.x>=hit.z-Extent.w&&bounds.y<=hit.z+Extent.w;
}
float4 sourceRadiance(float2 uv,float3 hit,float distance,float roughness,float3 pointRadiance) {
    if(Settings.z==0||Settings.w<1||roughness==0)return float4(pointRadiance,1);
    float4 clip=float4(dot(P0,float4(hit,1)),dot(P1,float4(hit,1)),dot(P2,float4(hit,1)),dot(P3,float4(hit,1)));
    if(clip.w<=0)return float4(pointRadiance,1);
    float3 gx=(P0.xyz-clip.x/clip.w*P3.xyz)/clip.w;
    float3 gy=(P1.xyz-clip.y/clip.w*P3.xyz)/clip.w;
    float worldRadius=2*roughness*roughness*distance;
    float radius=min(float(FORGE_REFLECTION_SOURCE_RADIUS),worldRadius*.5*max(length(gx)*Extent.x,length(gy)*Extent.y));
    if(!isfinite(radius)||radius<=.5)return float4(pointRadiance,1);
    float lod=clamp(log2(2*radius)-1,-1.,min(float(FORGE_REFLECTION_SOURCE_LEVELS-1),Settings.w-1));
    int requested=int(floor(lod));
    if(requested<0) {
        float4 sum;if(!sourceAt(0,uv,hit,sum))return float4(pointRadiance,1);
        sum=lerp(float4(pointRadiance,1),sum,lod+1);return float4(min(sum.rgb/sum.a,65504.),saturate(sum.a));
    }
    [loop]for(int level=requested;level>=0;--level) {
        float4 sum;if(!sourceAt(level,uv,hit,sum))continue;
        if(level==requested&&level+1<int(Settings.w)) {
            float4 coarse;if(sourceAt(level+1,uv,hit,coarse))sum=lerp(sum,coarse,frac(lod));
        }
        return float4(min(sum.rgb/sum.a,65504.),saturate(sum.a));
    }
    return float4(pointRadiance,1);
}
)";
    return result;
}
} // namespace forge
