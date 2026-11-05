#pragma once
namespace forge {
// Original far-depth reductions and bounded rectangular visibility queries.
inline constexpr const char* mesh_occlusion_compute = R"(
struct CullQuery {uint4 rectangle;float nearest;float bias;uint enabled;uint reserved;};
Texture2D<float> SourceDepth;
RWStructuredBuffer<float> PyramidWrite;
StructuredBuffer<float> PyramidRead;
StructuredBuffer<CullQuery> QueryBounds;
RWStructuredBuffer<uint> DrawArguments;
cbuffer OcclusionValues {uint4 Input;uint4 Output;uint4 Viewport;uint4 Queries;};
float sourceDepth(uint2 p) {
    if(any(p>=Viewport.zw))return 1.;
    float d=SourceDepth.Load(int3(p+Viewport.xy,0));
    return d>=0 && d<=1 ? d : 1.;
}
[numthreads(8,8,1)]
void seed(uint3 dispatch:SV_DispatchThreadID) {
    uint2 p=dispatch.xy;
    if(any(p>=Output.xy))return;
    uint2 at=p*2;
    float value=max(max(sourceDepth(at),sourceDepth(at+uint2(1,0))),
                    max(sourceDepth(at+uint2(0,1)),sourceDepth(at+uint2(1,1))));
    PyramidWrite[Output.z+p.y*Output.x+p.x]=value;
}
float parentDepth(uint2 p) {
    p=min(p,Input.xy-1);
    return PyramidWrite[Input.z+p.y*Input.x+p.x];
}
[numthreads(8,8,1)]
void reduce(uint3 dispatch:SV_DispatchThreadID) {
    uint2 p=dispatch.xy;
    if(any(p>=Output.xy))return;
    uint2 at=p*2;
    float value=max(max(parentDepth(at),parentDepth(at+uint2(1,0))),
                    max(parentDepth(at+uint2(0,1)),parentDepth(at+uint2(1,1))));
    PyramidWrite[Output.z+p.y*Output.x+p.x]=value;
}
[numthreads(64,1,1)]
void classify(uint3 dispatch:SV_DispatchThreadID) {
    uint index=dispatch.x;
    if(index>=Queries.x)return;
    CullQuery q=QueryBounds[index];
    if(q.enabled!=1 || any(q.rectangle.xy>=q.rectangle.zw) ||
       any(q.rectangle.zw>Viewport.zw) || !isfinite(q.nearest) ||
       q.nearest<0 || q.nearest>1 || !isfinite(q.bias) || q.bias<0 || q.bias>1)return;
    uint2 low=q.rectangle.xy/2,high=(q.rectangle.zw-1)/2;
    uint span=max(high.x-low.x+1,high.y-low.y+1),level=0,cell=1;
    [loop]while(cell<span && level+1<Output.w){cell*=2;++level;}
    uint offset=0;uint2 size=Output.xy;
    [loop]for(uint i=0;i<level;++i){offset+=size.x*size.y;size=max(size/2,uint2(1,1));}
    low>>=level;high>>=level;
    float farthest=max(max(PyramidRead[offset+low.y*size.x+low.x],
                          PyramidRead[offset+low.y*size.x+high.x]),
                      max(PyramidRead[offset+high.y*size.x+low.x],
                          PyramidRead[offset+high.y*size.x+high.x]));
    if(farthest>=0 && farthest<=1 && q.nearest>farthest+q.bias)
        DrawArguments[index*5+1]=0;
}
)";
} // namespace forge
