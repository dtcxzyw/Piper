#pragma once
#include "Microfact.hpp"
//Trowbridge-Reitz Distribution

namespace TrowbridgeReitzDistribution {
    DEVICE float roughnessToAlpha(float roughness) {
        roughness = fmaxf(roughness, 1e-3f);
        float x = logf(roughness);
        float res = 0.000640711f;
        res = res * x + 0.0171201f;
        res = res * x + 0.1734f;
        res = res * x + 0.819955f;
        return res * x + 1.62142f;
    }
    DEVICE float D(const Vec3 &wh, const Vec2 &alpha) {
        float tan2ThetaV = tan2Theta(wh);
        if (isinf(tan2ThetaV)) return 0.0f;
        float cos4Theta = sqr(cos2Theta(wh));
        float e = (cos2Phi(wh) / sqr(alpha.x) + sin2Phi(wh) / sqr(alpha.y)) * tan2ThetaV;
        return 1.0f / (pi * alpha.x * alpha.y * cos4Theta * sqr(1.0f + e));
    }
    DEVICE float lambda(const Vec3 &w, const Vec2 &alpha) {
        float absTanTheta = fabsf(tanTheta(w));
        if (isinf(absTanTheta)) return 0.0f;
        // Compute alpha for direction w
        float alphaW = sqrtf(cos2Phi(w) * sqr(alpha.x) + sin2Phi(w) * sqr(alpha.y));
        float alpha2Tan2Theta = sqr(alphaW * absTanTheta);
        return (-1.0f + sqrtf(1.0f + alpha2Tan2Theta)) * 0.5f;
    }
    DEVICE Vec2 trowbridgeReitzSample11(float cosTheta, float U1, float U2) {
        // special case (normal incidence)      
        if (cosTheta > 0.9999f) {
            float r = sqrtf(U1 / (1.0f - U1));
            float phi = twoPi * U2;
            return make_float2(cos(phi), sin(phi)) * r;
        }

        float sinTheta = cos2Sin(cosTheta);
        float tanTheta = sinTheta / cosTheta;
        float a = 1.0f / tanTheta;
        float G1 = 2.0f / (1.0f + sqrtf(1.0f + 1.0f / (a * a)));

        // sample slope_x
        float A = 2.0f * U1 / G1 - 1.0f, A2 = sqr(A);
        float tmp = fminf(1e10f, 1.0f / (A2 - 1.0f));
        float B = tanTheta, B2 = sqr(B);
        float D = sqrtf(fmaxf(B2 * tmp * tmp - (A2 - B2) * tmp, 0.0f));
        float Btmp = B * tmp;
        float slopeX1 = Btmp - D;
        float slopeX2 = Btmp + D;
        float slopeX = (A < 0.0f | slopeX2 > 1.0f / tanTheta) ? slopeX1 : slopeX2;

        // sample slope_y
        float S;
        if (U2 > 0.5f) {
            S = 1.0f;
            U2 = 2.0f * (U2 - 0.5f);
        }
        else {
            S = -1.0f;
            U2 = 2.0f * (0.5f - U2);
        }
        float z =
            (U2 * (U2 * (U2 * 0.27385f - 0.73369f) + 0.46341f)) /
            (U2 * (U2 * (U2 * 0.093073f + 0.309420f) - 1.000000f) + 0.597999f);
        float slopeY = S * z * sqrtf(1.0f + sqr(slopeX));
        return make_float2(slopeX, slopeY);
    }

    DEVICE Vec3 trowbridgeReitzSample(const Vec3 &wi, const Vec2 &alpha,
        const Vec2 &u) {
// 1. stretch wi
        Vec3 wiStretched =
            normalize(make_float3(alpha.x * wi.x, alpha.y * wi.y, wi.z));

        // 2. simulate P22_{wi}(x_slope, y_slope, 1, 1)

        Vec2 slope = trowbridgeReitzSample11(cosTheta(wiStretched), u.x, u.y);

        // 3. rotate    
        float sinW = sinPhi(wiStretched), cosW = cosPhi(wiStretched);
        float tmpX = cosW * slope.x - sinW * slope.y;
        slope.y = sinW * slope.x + cosW * slope.y;
        slope.x = tmpX;

        // 4. unstretch
        slope *= alpha;

        // 5. compute normal
        return normalize(make_float3(-slope.x, -slope.y, 1.0f));
    }

    DEVICE Vec3 sampleWh(const Vec3 &wo, const Vec2 &alpha, const Vec2 &u) {
        Vec3 wh = trowbridgeReitzSample(wo.z >= 0.0f ? wo : -wo, alpha, u);
        return wo.z >= 0.0f ? wh : -wh;
    }

    DEVICE float pdf(const Vec3 &wo, const Vec3 &wh, const Vec2 &alpha) {
        float G1 = 1.0f / (1.0f + lambda(wo, alpha));
        return D(wh, alpha) * G1 * fabsf(dot(wo, wh)) / absCosTheta(wo);
    }
    DEVICE float G(const Vec3 &wo, const Vec3 &wi, const Vec2 &alpha) {
        return 1.0f / (1.0f + lambda(wo, alpha) + lambda(wi, alpha));
    }
}
