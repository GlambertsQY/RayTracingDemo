#ifndef RT_HDR_GLSL
#define RT_HDR_GLSL

// HDR10（SMPTE ST 2084 / PQ）输出相关常量与转换函数

// SDR 参考白（paper white）亮度，单位 nits
const float kPaperWhiteNits = 203.0;

// 线性 Rec.709 原色 → 线性 Rec.2020 原色
vec3 rec709ToRec2020(vec3 color)
{
    return vec3(dot(color, vec3(0.6274039, 0.3292830, 0.0433131)),
                dot(color, vec3(0.0690973, 0.9195404, 0.0113623)),
                dot(color, vec3(0.0163914, 0.0880133, 0.8955953)));
}

// 线性亮度（0..1 对应 0..10000 nits）→ PQ 编码值
vec3 linearToPq(vec3 linear)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    const vec3 l = pow(max(linear, vec3(0.0)), vec3(m1));
    return pow((c1 + c2 * l) / (1.0 + c3 * l), vec3(m2));
}

#endif // RT_HDR_GLSL
