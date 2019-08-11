// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl

#include "../../Common.hpp"

//=================================================================================================
//
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

// The code in this file was originally written by
// Stephen Hill (@self_shadow), who deserves all credit
// for coming up with this fit and implementing it. Buy
// him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const Vec3 ACESInputMat[3] = {
    { 0.59719f, 0.35458f, 0.04823f },
    { 0.07600f, 0.90834f, 0.01566f },
    { 0.02840f, 0.13383f, 0.83777f }
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const Vec3 ACESOutputMat[3] = {
    { 1.60475f, -0.53108f, -0.07367f },
    { -0.10208f, 1.10813f, -0.00605f },
    { -0.00327f, -0.07276f, 1.07602f }
};

Vec3 RRTAndODTFit(Vec3 v) {
    Vec3 a = v * (v + 0.0245786f) - 0.000090537f;
    Vec3 b =
        v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

Spectrum mul(const Vec3 (&mat)[3], Spectrum c) {
    return make_float3(dot(mat[0], c), dot(mat[1], c),
                    dot(mat[2], c));
}

Spectrum ACESFitted(Spectrum color) {
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    return color * 1.8f;
}
