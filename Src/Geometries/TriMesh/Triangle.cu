#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __closesthit__RCH() {
    const DataDesc* data = getSBTData<DataDesc>();
    float2 uv = optixGetTriangleBarycentrics();
    float u = uv.x, v = uv.y, w = 1.0f - u - v;
    Payload* payload = getPayload();
    Uint3 idx = data->index[optixGetPrimitiveIndex()];
    Vec3 p0 = f2v(
             optixTransformPointFromObjectToWorldSpace(v2f(data->vertex[0]))),
         p1 = f2v(
             optixTransformPointFromObjectToWorldSpace(v2f(data->vertex[1]))),
         p2 = f2v(
             optixTransformPointFromObjectToWorldSpace(v2f(data->vertex[2])));
    Vec3 ng = glm::cross(p1 - p0, p2 - p0);
    bool front = optixIsTriangleFrontFaceHit();
    Vec3 ns;
    if(data->normal) {
        ns = data->normal[idx.x] * u + data->normal[idx.y] * v +
            data->normal[idx.z] * w;
        ns = f2v(optixTransformNormalFromObjectToWorldSpace(v2f(ns)));
        ns = (glm::dot(ns, ng) > 0.0f ? ns : -ns);
    } else
        ns = ng;

    Vec2 texCoord = { 0.0f, 0.0f };
    if(data->texCoord)
        texCoord = data->texCoord[idx.x] * u + data->texCoord[idx.y] * v +
            data->texCoord[idx.z] * w;

    Vec3 ori = f2v(optixGetWorldRayOrigin());
    Vec3 dir = f2v(optixGetWorldRayDirection());
    Vec3 hit = ori + optixGetRayTmin() * dir;
    builtinMaterialSample(data->material, payload, dir, hit, ng, ns, texCoord,
                          optixGetRayTime(), front);
}

GLOBAL void __anyhit__OAH() {}
