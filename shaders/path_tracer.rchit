#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require

#include "shared/common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload gPayload;

void main()
{
    vec3 p0;
    vec3 p1;
    vec3 p2;
    vec3 n0;
    vec3 n1;
    vec3 n2;
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    fetchTriangle(uint(gl_InstanceCustomIndexEXT), uint(gl_PrimitiveID), p0, p1, p2, n0, n1, n2, uv0, uv1, uv2);

    const vec3 world0 = objectToWorldPoint(gl_ObjectToWorldEXT, p0);
    const vec3 world1 = objectToWorldPoint(gl_ObjectToWorldEXT, p1);
    const vec3 world2 = objectToWorldPoint(gl_ObjectToWorldEXT, p2);

    const vec3 hitPosition = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    const vec3 bary = barycentric(hitPosition, world0, world1, world2);

    vec3 normal = objectToWorldNormal(gl_ObjectToWorldEXT, n0) * bary.x +
                  objectToWorldNormal(gl_ObjectToWorldEXT, n1) * bary.y +
                  objectToWorldNormal(gl_ObjectToWorldEXT, n2) * bary.z;
    if (!isFiniteVec(normal) || dot(normal, normal) < 1e-10)
    {
        normal = cross(world1 - world0, world2 - world0);
    }
    if (!isFiniteVec(normal) || dot(normal, normal) < 1e-10)
    {
        normal = vec3(0.0, 1.0, 0.0);
    }

    gPayload.normal = normalize(normal);
    gPayload.t = gl_HitTEXT;
    gPayload.uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    gPayload.instance = uint(gl_InstanceCustomIndexEXT);
    gPayload.primitive = uint(gl_PrimitiveID);
}
