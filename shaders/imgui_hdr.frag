#version 450
#extension GL_GOOGLE_include_directive : require

#include "shared/hdr.glsl"

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform texture2D _Texture;
layout(set = 1, binding = 0) uniform sampler _Sampler;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

void main()
{
    const vec4 c = In.Color * texture(sampler2D(_Texture, _Sampler), In.UV.st);
    // UI 颜色按 sRGB 处理，映射到纸白亮度后做 PQ 编码，保证在 HDR10 目标上观感一致
    const vec3 linear = pow(max(c.rgb, vec3(0.0)), vec3(2.2));
    fColor = vec4(linearToPq(linear * kPaperWhiteNits / 10000.0), c.a);
}
