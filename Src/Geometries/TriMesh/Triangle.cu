#include "DataDesc.hpp"
#include "../../CUDA.hpp"

rtDeclareVariable(optix::Ray, currentRay, rtCurrentRay, );
rtDeclareVariable(Vec3, shadingNormal, attribute shadingNormal, );
rtDeclareVariable(Vec3, geometricNormal, attribute geometricNormal, );
rtDeclareVariable(Vec2, texCoord, attribute texCoord, );
rtBuffer<Vertex> geometryVertex;
rtBuffer<uint3> geometryIndex;

RT_PROGRAM void intersect(int index) {
    uint3 idx = geometryIndex[index];
    Vertex v0 = geometryVertex[idx.x], v1 = geometryVertex[idx.y],
        v2 = geometryVertex[idx.z];
    Vec3 n;
    float t, beta, gamma;
    //TODO:early exit version
    bool res = optix::intersect_triangle_branchless(currentRay, v0.pos, v1.pos, v2.pos,
        n, t, beta, gamma);
    if (rtPotentialIntersection(t)) {
        geometricNormal = n;
        float alpha = 1.0f - (beta + gamma);
        shadingNormal = v0.normal * beta + v1.normal * gamma +
            v2.normal * alpha;
        texCoord = v0.texCoord * beta + v1.texCoord * gamma + v2.texCoord * alpha;
        rtReportIntersection(0);
    }
}

RT_PROGRAM void bounds(int index, float result[6]) {
    optix::Aabb &aabb = *reinterpret_cast<optix::Aabb *>(result);
    uint3 idx = geometryIndex[index];
    aabb.include(geometryVertex[idx.x].pos);
    aabb.include(geometryVertex[idx.y].pos);
    aabb.include(geometryVertex[idx.z].pos);
}
