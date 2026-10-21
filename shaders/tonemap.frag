#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "shared/common.glsl"

layout(location = 0) out vec4 gOutColor;

void main()
{
    const vec2 uv = gl_FragCoord.xy / max(fc.outputResolution, vec2(1.0));
    const ivec2 renderSize = ivec2(fc.resolution);
    const ivec2 pixel = clamp(ivec2(uv * fc.resolution), ivec2(0), max(renderSize - 1, ivec2(0)));
    const vec4 accumulated = imageLoad(gAccumImage, pixel);
    const vec3 hdr = accumulated.rgb / max(accumulated.a, 1.0) * fc.exposure;
    const vec3 mapped = acesTonemap(hdr);
    gOutColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
