#include "../../Shared/KernelShared.hpp"

/*
 * Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//from cuda/intersection_refinement.h
// Plane intersection -- used for refining triangle hit points.  Note
// that this skips zero denom check (for rays perpindicular to plane normal)
// since we know that the ray intersects the plane.
static INLINEDEVICE float intersectPlane(const optix::float3 &origin,
    const optix::float3 &direction,
    const optix::float3 &normal,
    const optix::float3 &point) {
// Skipping checks for non-zero denominator since we know that ray intersects this plane
    return -(optix::dot(normal, origin - point)) / optix::dot(normal, direction);

}

// Offset the hit point using integer arithmetic
static INLINEDEVICE optix::float3 offset(const optix::float3 &hit_point, const optix::float3 &normal) {
    using namespace optix;

    const float epsilon = 1.0e-4f;
    const float offset = 4096.0f * 2.0f;

    float3 offset_point = hit_point;
    if ((__float_as_int(hit_point.x) & 0x7fffffff) < __float_as_int(epsilon)) {
        offset_point.x += epsilon * normal.x;
    }
    else {
        offset_point.x = __int_as_float(__float_as_int(offset_point.x) + int(copysign(offset, hit_point.x) * normal.x));
    }

    if ((__float_as_int(hit_point.y) & 0x7fffffff) < __float_as_int(epsilon)) {
        offset_point.y += epsilon * normal.y;
    }
    else {
        offset_point.y = __int_as_float(__float_as_int(offset_point.y) + int(copysign(offset, hit_point.y) * normal.y));
    }

    if ((__float_as_int(hit_point.z) & 0x7fffffff) < __float_as_int(epsilon)) {
        offset_point.z += epsilon * normal.z;
    }
    else {
        offset_point.z = __int_as_float(__float_as_int(offset_point.z) + int(copysign(offset, hit_point.z) * normal.z));
    }

    return offset_point;
}

// Refine the hit point to be more accurate and offset it for reflection and
// refraction ray starting points.
static INLINEDEVICE void refine_and_offset_hitpoint(const optix::float3 &original_hit_point, const optix::float3 &direction,
    const optix::float3 &normal, const optix::float3 &p, optix::float3 &back_hit_point,
    optix::float3 &front_hit_point) {
    using namespace optix;

    // Refine hit point
    float  refined_t = intersectPlane(original_hit_point, direction, normal, p);
    float3 refined_hit_point = original_hit_point + refined_t * direction;

    // Offset hit point
    if (dot(direction, normal) > 0.0f) {
        back_hit_point = offset(refined_hit_point, normal);
        front_hit_point = offset(refined_hit_point, -normal);
    }
    else {
        back_hit_point = offset(refined_hit_point, -normal);
        front_hit_point = offset(refined_hit_point, normal);
    }
}

rtDeclareVariable(Vec3, backHitPoint, attribute backHitPoint, );
rtDeclareVariable(Vec3, frontHitPoint, attribute frontHitPoint, );
rtDeclareVariable(Vec3, normal, attribute normal, );
rtDeclareVariable(Vec2, texCoord, attribute texCoord, );

rtBuffer<float3> geometryVertexBuffer;
rtBuffer<uint3> geometryIndexBuffer;
rtBuffer<Vec3> geometryNormalBuffer;
rtBuffer<Vec2> geometryTexCoordBuffer;

rtDeclareVariable(optix::Ray, ray, rtCurrentRay, );

INLINEDEVICE void setAttributes(const uint3 &idx, const Vec3 &ng, 
    const Vec3 &hit, const Vec3 &refPoint, float u, float v, float w) {
    if (geometryNormalBuffer.size())
        normal = geometryNormalBuffer[idx.x] * u
        + geometryNormalBuffer[idx.y] * v
        + geometryNormalBuffer[idx.z] * w;
    else normal = normalize(ng);

    if (dot(ray.direction, normal) > 0.0f)
        normal = -normal;

    if (geometryTexCoordBuffer.size())
        texCoord = geometryTexCoordBuffer[idx.x] * u
        + geometryTexCoordBuffer[idx.y] * v
        + geometryTexCoordBuffer[idx.z] * w;
    else texCoord = make_float2(0.0f);

    refine_and_offset_hitpoint(hit, ray.direction, ng, refPoint,
        backHitPoint, frontHitPoint);
}

//from cuda/triangle_mesh.cu
RT_PROGRAM void meshAttributes() {
    const uint3 idx = geometryIndexBuffer[rtGetPrimitiveIndex()];
    const Vec3 v0 = geometryVertexBuffer[idx.x];
    const Vec3 v1 = geometryVertexBuffer[idx.y];
    const Vec3 v2 = geometryVertexBuffer[idx.z];
    const Vec3 ng = cross(v1 - v0, v2 - v0);

    const Vec2 bar = rtGetTriangleBarycentrics();
    float u = 1.0f - bar.x - bar.y, v = bar.x, w = bar.y;
    setAttributes(idx, ng, v0 * u + v1 * v + v2 * w, v0, u, v, w);
}

RT_PROGRAM void bounds(int primIdx, float result[6]) {
    const uint3 idx = geometryIndexBuffer[primIdx];
    optix::Aabb *aabb = reinterpret_cast<optix::Aabb *>(result);
    aabb->include(geometryVertexBuffer[idx.x]);
    aabb->include(geometryVertexBuffer[idx.y]);
    aabb->include(geometryVertexBuffer[idx.z]);
}

RT_PROGRAM void intersect(int primIdx) {
    const uint3 idx = geometryIndexBuffer[primIdx];

    const float3 p0 = geometryVertexBuffer[idx.x];
    const float3 p1 = geometryVertexBuffer[idx.y];
    const float3 p2 = geometryVertexBuffer[idx.z];

    // Intersect ray with triangle
    float3 n;
    float  t, beta, gamma;
    if (intersect_triangle(ray, p0, p1, p2, n, t, beta, gamma)) {
        if (rtPotentialIntersection(t)) {
            float alpha = 1.0f - beta - gamma;
            setAttributes(idx, n, ray.origin + t * ray.direction, p0, alpha, beta, gamma);
            rtReportIntersection(0);
        }
    }
}
