#include "engine/forge.surface.hlsli"
float4 GalleryPanel(ForgeUiInput input) {
    float speed = ForgeParameter_speed();
    float band = 0.65 + 0.35 * cos(6.2831853 * (input.UV.x - input.Time * speed));
    float4 image = ForgeSample_image(input);
    return float4(image.rgb * ForgeParameter_tint().rgb * band,
                  image.a * ForgeParameter_tint().a);
}
