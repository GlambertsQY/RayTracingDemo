#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require

#include "shared/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload gPayload;

void main()
{
    gPayload.t = -1.0;
}
