#ifndef RT_VOLUMETRIC_GLSL
#define RT_VOLUMETRIC_GLSL

// 均匀体积雾（单次散射近似）

float fogSigmaT()
{
    return max(fc.fogDensity, 0.0);
}

float fogSigmaS()
{
    return fogSigmaT() * clamp(fc.fogAlbedo, 0.0, 1.0);
}

float fogTransmittance(float distance)
{
    const float sigmaT = fogSigmaT();
    return sigmaT > 1e-6 ? exp(-sigmaT * max(distance, 0.0)) : 1.0;
}

// Henyey-Greenstein 相函数：g > 0 表示前向散射
float hgPhase(float cosTheta, float g)
{
    const float g2 = g * g;
    const float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * kPi * denom * sqrt(denom));
}

float sampleHgCos(float g)
{
    const float u = rand();
    if (abs(g) < 0.001)
    {
        return 1.0 - 2.0 * u;
    }
    const float s = (1.0 - g * g) / (1.0 + g - 2.0 * g * u);
    return clamp(-(1.0 + g * g - s * s) / (2.0 * g), -1.0, 1.0);
}

vec3 sampleHgDirection(vec3 direction, float g)
{
    const float cosTheta = sampleHgCos(g);
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * kPi * rand();
    vec3 tangent;
    vec3 bitangent;
    buildOnb(direction, tangent, bitangent);
    return normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) +
                     direction * cosTheta);
}

// 按指数分布采样介质中的散射距离
float sampleMediumDistance()
{
    const float sigmaT = fogSigmaT();
    if (sigmaT <= 1e-6)
    {
        return 1e30;
    }
    return -log(max(1.0 - rand(), 1e-6)) / sigmaT;
}

#endif // RT_VOLUMETRIC_GLSL
