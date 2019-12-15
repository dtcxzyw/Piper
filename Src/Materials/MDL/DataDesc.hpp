#pragma once
#include "../../Shared/Shared.hpp"
#pragma warning(push, 0)
#include <mi/mdl_sdk.h>
#include <vector_functions.hpp>
#pragma warning(pop)
namespace MDL = mi::neuraylib;

// From examples/mdl_sdk/shared/texture_support_cuda.h

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_ONE_OVER_PI 0.318309886183790671538

using MDL::Mbsdf_part;
using MDL::Tex_wrap_mode;
using MDL::Texture_handler_base;

// Custom structure representing an MDL texture, containing filtered and
// unfiltered CUDA texture objects and the size of the texture.
struct Texture final {
    CUtexObject filtered_object;    // uses filter mode cudaFilterModeLinear
    CUtexObject unfiltered_object;  // uses filter mode cudaFilterModePoint
    uint3 size;       // size of the texture, needed for texel access
    float3 inv_size;  // the inverse values of the size of the texture
};

// Custom structure representing an MDL BSDF measurement.
struct Mbsdf {
    unsigned has_data[2];  // true if there is a measurement for this part
    CUtexObject eval_data[2];  // uses filter mode cudaFilterModeLinear
    float max_albedo[2];    // max albedo used to limit the multiplier
    float* sample_data[2];  // CDFs for sampling a BSDF measurement
    float* albedo_data[2];  // max albedo for each theta (isotropic)

    uint2
        angular_resolution[2];  // size of the dataset, needed for texel access
    float2 inv_angular_resolution[2];  // the inverse values of the size of the
                                       // dataset
    unsigned num_channels[2];          // number of color channels (1 or 3)
};

// Structure representing a Light Profile
struct Lightprofile {
    __device__ explicit Lightprofile()
        : angular_resolution(make_uint2(0, 0)),
          theta_phi_start(make_float2(0.0f, 0.0f)),
          theta_phi_delta(make_float2(0.0f, 0.0f)),
          theta_phi_inv_delta(make_float2(0.0f, 0.0f)),
          candela_multiplier(0.0f), total_power(0.0f), eval_data(0) {}

    uint2 angular_resolution;    // angular resolution of the grid
    float2 theta_phi_start;      // start of the grid
    float2 theta_phi_delta;      // angular step size
    float2 theta_phi_inv_delta;  // inverse step size
    float candela_multiplier;    // factor to rescale the normalized data
    float total_power;

    CUtexObject eval_data;  // normalized data sampled on grid
    float* cdf_data;                // CDFs for sampling a light profile
};

// The texture handler structure required by the MDL SDK with custom additional
// fields.
struct Texture_handler : MDL::Texture_handler_base {
    // additional data for the texture access functions can be provided here

    size_t num_textures;      // the number of textures used by the material
                              // (without the invalid texture)
    Texture const* textures;  // the textures used by the material
                              // (without the invalid texture)

    size_t num_mbsdfs;    // the number of mbsdfs used by the material
                          // (without the invalid mbsdf)
    Mbsdf const* mbsdfs;  // the mbsdfs used by the material
                          // (without the invalid mbsdf)

    size_t num_lightprofiles;  // number of elements in the lightprofiles field
                               // (without the invalid light profile)
    Lightprofile const*
        lightprofiles;  // a device pointer to a list of mbsdfs objects, if used
                        // (without the invalid light profile)
};

struct DataDesc final {
    // All DF_* functions of one material DF use the same target argument block.
    const char* argData;
};
