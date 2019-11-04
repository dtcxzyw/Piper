#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __closesthit__RCH() {
    const DataDesc* data = getSBTData<DataDesc>();
    float2 uv = optixGetTriangleBarycentrics();
    float u = uv.x, v = uv.y, w = 1.0f - u - v;
    Payload* payload = getPayload();
    Uint3 idx = data->index[optixGetPrimitiveIndex()];
    Vec3 p0 = data->vertex[idx.x], p1 = data->vertex[idx.y],
         p2 = data->vertex[idx.z];
    Vec3 ng = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    ng = f2v(optixTransformNormalFromObjectToWorldSpace(v2f(ng)));
    bool front = optixIsTriangleFrontFaceHit();
    Vec3 ns;
    if(data->normal) {
        ns = glm::normalize(data->normal[idx.x] * u + data->normal[idx.y] * v +
                            data->normal[idx.z] * w);
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
