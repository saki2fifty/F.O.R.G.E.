#pragma once
#include <string>
#include <string_view>

namespace forge {
// Shared Sprite slice/fill/trim/turn and frame-mip shader source.
// Keep all consumers on one coordinate authority.
inline constexpr std::string_view sprite_image_shader_declarations = R"(
float ForgeSpriteSlice(float u,float4 axis) {
    if(axis.z>0 && u<=axis.z)return u*axis.x/axis.z;
    if(axis.w>0 && u>=1-axis.w)return 1-(1-u)*axis.y/axis.w;
    float center=1-axis.z-axis.w;
    return center>0?axis.x+(u-axis.z)*(1-axis.x-axis.y)/center:axis.x;
}
)";
inline constexpr std::string_view sprite_image_coordinates_shader(bool frame_mips) {
    if (frame_mips)
        return R"(
    // Derived images contain the complete logical canvas. Gradients must use
    // continuous coordinates before tiled wrapping introduces discontinuities.
    float2 spriteContinuous=input.UV[0];
    if(input.SpriteImage.x!=0) {
        float2 local=input.UV[0];
        if(input.SpriteAtlas.z<0)local.x=1-local.x;
        if(input.SpriteAtlas.w<0)local.y=1-local.y;
        spriteContinuous=input.SpriteImage.x==1?
            float2(ForgeSpriteSlice(local.x,input.SpriteX),ForgeSpriteSlice(local.y,input.SpriteY)):
            local*input.SpriteImage.yz;
    }
    spriteDx=ddx(spriteContinuous);spriteDy=ddy(spriteContinuous);
    input.UV[0]=input.SpriteImage.x==2?frac(spriteContinuous):spriteContinuous;
)";
    return R"(
    // Simple mode reuses the otherwise unused slice row for the retained quad
    // rectangle. Identity frames take the original path without inverse math.
    if(input.SpriteImage.x==0 && any(input.SpriteX!=float4(0,0,1,1))) {
        float2 local=(input.UV[0]-input.SpriteAtlas.xy)/input.SpriteAtlas.zw;
        if(input.SpriteImage.w!=0)local=local.yx;
        if(any(local<input.SpriteX.xy) || any(local>input.SpriteX.xy+input.SpriteX.zw))return false;
        local=(local-input.SpriteX.xy)/input.SpriteX.zw;
        if(input.SpriteImage.w!=0)local=local.yx;
        input.UV[0]=input.SpriteAtlas.xy+local*input.SpriteAtlas.zw;
    }
    if(input.SpriteImage.x!=0) {
        float2 local=input.UV[0];
        bool rotated=input.SpriteImage.w!=0;
        bool flipX=rotated?input.SpriteAtlas.w<0:input.SpriteAtlas.z<0;
        bool flipY=rotated?input.SpriteAtlas.z>0:input.SpriteAtlas.w<0;
        if(flipX)local.x=1-local.x;
        if(flipY)local.y=1-local.y;
        float2 mapped=input.SpriteImage.x==1?
            float2(ForgeSpriteSlice(local.x,input.SpriteX),ForgeSpriteSlice(local.y,input.SpriteY)):
            frac(local*input.SpriteImage.yz);
        // Fill uses the original logical canvas. The retained rectangle is
        // supplied after logical flips; undo those flips for source-space trim.
        float4 crop=input.SpriteCrop;
        if(flipX)crop.x=1-crop.x-crop.z;
        if(flipY)crop.y=1-crop.y-crop.w;
        if(any(mapped<crop.xy) || any(mapped>crop.xy+crop.zw))return false;
        mapped=(mapped-crop.xy)/crop.zw;
        if(rotated)mapped=float2(1-mapped.y,mapped.x);
        input.UV[0]=min(input.SpriteAtlas.xy,input.SpriteAtlas.xy+input.SpriteAtlas.zw)+
                    mapped*abs(input.SpriteAtlas.zw);
    }
    input.UV[0]=clamp(input.UV[0],input.SpriteClamp.xy,input.SpriteClamp.zw);
)";
}
// Visibility is separate from sampling: ordinary Sprite draws discard outside
// retained pixels; a custom function may generate color there after sampling zero.
inline std::string sprite_image_coordinate_function(std::string_view input_type, bool frame_mips) {
    return std::string(sprite_image_shader_declarations) +
           "bool ForgeSpriteImageCoordinates(inout " + std::string(input_type) +
           " input,out float2 spriteDx,out float2 spriteDy){spriteDx=0;spriteDy=0;\n" +
           std::string(sprite_image_coordinates_shader(frame_mips)) + "return true;}\n";
}
} // namespace forge
