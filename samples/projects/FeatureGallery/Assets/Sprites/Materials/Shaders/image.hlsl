#include "engine/forge.surface.hlsli"
float4 GalleryImage(ForgeSpriteInput input) {
    return ForgeSample_image(input) * ForgeParameter_gain() * input.Color;
}
