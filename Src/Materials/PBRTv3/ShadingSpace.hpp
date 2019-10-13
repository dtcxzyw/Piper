#pragma once
#include "../../Shared/KernelShared.hpp"

// TODO:tangent from mesh
DEVICE void calcShadingSpace(const Vec3& dir, const Vec3& ng, Vec3& ns,
                             bool front, Vec3& wo, Vec3& frontOffset,
                             Mat3& w2s) {
    ns = glm::normalize(ns);
    Vec3 bx = { 1.0f, 0.0f, 0.0f }, by = { 0.0f, 1.0f, 0.0f };
    Vec3 t = glm::normalize(fabs(ns.x) < fabs(ns.y) ? glm::cross(ns, bx) :
                                                      glm::cross(ns, by));
    Vec3 bt = glm::cross(t, ns);
    w2s = Mat3{ t, bt, ns };
    wo = w2s * glm::normalize(-dir);
    frontOffset = (front ? ng : -ng) * (1e-3f / glm::length(ng));
}
