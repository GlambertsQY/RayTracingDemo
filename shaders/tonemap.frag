#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "shared/common.glsl"

layout(location = 0) out vec4 gOutColor;

void main()
{
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const vec4 accumulated = imageLoad(gAccumImage, pixel);
    const vec3 hdr = accumulated.rgb / max(accumulated.a, 1.0) * fc.exposure;
    const vec3 mapped = acesTonemap(hdr);
    gOutColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
