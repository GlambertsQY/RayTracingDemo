#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "shared/common.glsl"
#include "shared/hdr.glsl"

layout(location = 0) out vec4 gOutColor;

layout(set = 0, binding = 11) uniform sampler2D gBloomImage;

void main()
{
    const vec2 uv = gl_FragCoord.xy / max(fc.outputResolution, vec2(1.0));
    const ivec2 renderSize = ivec2(fc.resolution);
    const ivec2 pixel = clamp(ivec2(uv * fc.resolution), ivec2(0), max(renderSize - 1, ivec2(0)));
    const vec4 accumulated = imageLoad(gAccumImage, pixel);

    vec3 hdr = accumulated.rgb / max(accumulated.a, 1.0) * fc.exposure;
    hdr += texture(gBloomImage, uv).rgb * fc.bloomStrength;

    if ((fc.flags & 1u) != 0u)
    {
        // HDR10：按亮度滚降并保持色度，避免彩色高光过曝成白色
        const float paperWhite = max(fc.paperWhiteNits, 1.0);
        const float peakRatio = max(fc.hdrPeakNits / paperWhite, 1.0);
        const vec3 sceneLinear = max(hdr, vec3(0.0));
        const float luminance = max(dot(sceneLinear, vec3(0.2126, 0.7152, 0.0722)), 1e-6);
        const float mappedLuminance = luminance * peakRatio / (peakRatio - 1.0 + luminance);

        vec3 mapped = sceneLinear * (mappedLuminance / luminance);
        const float maxChannel = max(max(mapped.r, mapped.g), mapped.b);
        mapped *= min(1.0, peakRatio / max(maxChannel, 1e-6));

        const vec3 nits = rec709ToRec2020(mapped) * paperWhite;
        gOutColor = vec4(linearToPq(nits / 10000.0), 1.0);
    }
    else
    {
        const vec3 mapped = acesTonemap(hdr);
        gOutColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
    }
}
