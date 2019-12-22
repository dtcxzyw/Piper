// From examples/mdl_sdk/shared/example_cuda_shared.h
/******************************************************************************
 * Copyright 2019 NVIDIA Corporation. All rights reserved.
 *****************************************************************************/

// Code shared by CUDA MDL SDK examples

#include "../../Shared/PluginShared.hpp"
#include "MDLShared.hpp"
#include <sstream>
#include <string>
#include <vector>

BUS_MODULE_NAME("Piper.BuiltinMaterial.MDL.CUDAHelper");

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_ONE_OVER_PI 0.318309886183790671538

#define check_success(expr)                                        \
    do {                                                           \
        if(!(expr)) {                                              \
            BUS_TRACE_THROW(std::runtime_error(#expr " failed.")); \
        }                                                          \
    } while(false)

// Structure representing an MDL texture, containing filtered and unfiltered
// CUDA texture objects and the size of the texture.
struct Texture {
    explicit Texture(CUtexObject filtered_object, CUtexObject unfiltered_object,
                     Uint3 size)
        : filtered_object(filtered_object),
          unfiltered_object(unfiltered_object), size(size),
          inv_size(1.0f / size.x, 1.0f / size.y, 1.0f / size.z) {}

    CUtexObject filtered_object;    // uses filter mode cudaFilterModeLinear
    CUtexObject unfiltered_object;  // uses filter mode cudaFilterModePoint
    Uint3 size;     // size of the texture, needed for texel access
    Vec3 inv_size;  // the inverse values of the size of the texture
};

// Structure representing an MDL bsdf measurement.
struct Mbsdf {
    explicit Mbsdf() {
        for(unsigned i = 0; i < 2; ++i) {
            has_data[i] = 0u;
            eval_data[i] = 0;
            sample_data[i] = 0;
            albedo_data[i] = 0;
            this->max_albedo[i] = 0.0f;
            angular_resolution[i] = { 0u, 0u };
            inv_angular_resolution[i] = { 0.0f, 0.0f };
            num_channels[i] = 0;
        }
    }

    void Add(MDL::Mbsdf_part part, const Uint2& angular_resolution,
             unsigned num_channels) {
        unsigned part_idx = static_cast<unsigned>(part);

        this->has_data[part_idx] = 1u;
        this->angular_resolution[part_idx] = angular_resolution;
        this->inv_angular_resolution[part_idx] = {
            1.0f / float(angular_resolution.x),
            1.0f / float(angular_resolution.y)
        };
        this->num_channels[part_idx] = num_channels;
    }

    unsigned has_data[2];      // true if there is a measurement for this part
    CUtexObject eval_data[2];  // uses filter mode cudaFilterModeLinear
    float max_albedo[2];       // max albedo used to limit the multiplier
    float* sample_data[2];     // CDFs for sampling a BSDF measurement
    float* albedo_data[2];     // max albedo for each theta (isotropic)

    Uint2
        angular_resolution[2];  // size of the dataset, needed for texel access
    Vec2 inv_angular_resolution[2];  // the inverse values of the size of the
                                     // dataset
    unsigned num_channels[2];        // number of color channels (1 or 3)
};

// Structure representing a Light Profile
struct Lightprofile {
    explicit Lightprofile(Uint2 angular_resolution = { 0, 0 },
                          Vec2 theta_phi_start = { 0.0f, 0.0f },
                          Vec2 theta_phi_delta = { 0.0f, 0.0f },
                          float candela_multiplier = 0.0f,
                          float total_power = 0.0f, CUtexObject eval_data = 0,
                          float* cdf_data = nullptr)
        : angular_resolution(angular_resolution),
          theta_phi_start(theta_phi_start), theta_phi_delta(theta_phi_delta),
          theta_phi_inv_delta({ 0.0f, 0.0f }),
          candela_multiplier(candela_multiplier), total_power(total_power),
          eval_data(eval_data), cdf_data(cdf_data) {
        theta_phi_inv_delta.x =
            theta_phi_delta.x ? (1.f / theta_phi_delta.x) : 0.f;
        theta_phi_inv_delta.y =
            theta_phi_delta.y ? (1.f / theta_phi_delta.y) : 0.f;
    }

    Uint2 angular_resolution;  // angular resolution of the grid
    Vec2 theta_phi_start;      // start of the grid
    Vec2 theta_phi_delta;      // angular step size
    Vec2 theta_phi_inv_delta;  // inverse step size
    float candela_multiplier;  // factor to rescale the normalized data
    float total_power;

    CUtexObject eval_data;  // normalized data sampled on grid
    float* cdf_data;        // CDFs for sampling a light profile
};

// Structure representing the resources used by the generated code of a target
// code.
struct Target_code_data {
    Target_code_data(size_t num_textures, CUdeviceptr textures,
                     size_t num_mbsdfs, CUdeviceptr mbsdfs,
                     size_t num_lightprofiles, CUdeviceptr lightprofiles,
                     CUdeviceptr ro_data_segment)
        : num_textures(num_textures), textures(textures),
          num_mbsdfs(num_mbsdfs), mbsdfs(mbsdfs),
          num_lightprofiles(num_lightprofiles), lightprofiles(lightprofiles),
          ro_data_segment(ro_data_segment) {}

    size_t num_textures;  // number of elements in the textures field
    CUdeviceptr
        textures;  // a device pointer to a list of Texture objects, if used

    size_t num_mbsdfs;  // number of elements in the mbsdfs field
    CUdeviceptr
        mbsdfs;  // a device pointer to a list of mbsdfs objects, if used

    size_t num_lightprofiles;  // number of elements in the lightprofiles field
    CUdeviceptr
        lightprofiles;  // a device pointer to a list of mbsdfs objects, if used

    CUdeviceptr ro_data_segment;  // a device pointer to the read-only data
                                  // segment, if used
};

//------------------------------------------------------------------------------
//
// Helper functions
//
//------------------------------------------------------------------------------

// Return a textual representation of the given value.
template <typename T>
std::string to_string(T val) {
    std::ostringstream stream;
    stream << val;
    return stream.str();
}

//------------------------------------------------------------------------------
//
// CUDA helper functions
//
//------------------------------------------------------------------------------

/// Helper struct to delete CUDA (and related) resources.
template <typename T>
struct Resource_deleter {
    /*compile error*/
};

template <>
struct Resource_deleter<CUarray> {
    void operator()(CUarray res) {
        checkCudaError(cuArrayDestroy(res));
    }
};

template <>
struct Resource_deleter<CUmipmappedArray> {
    void operator()(CUmipmappedArray res) {
        checkCudaError(cuMipmappedArrayDestroy(res));
    }
};

template <>
struct Resource_deleter<Texture> {
    void operator()(Texture& res) {
        checkCudaError(cuTexObjectDestroy(res.filtered_object));
        checkCudaError(cuTexObjectDestroy(res.unfiltered_object));
    }
};

template <>
struct Resource_deleter<Mbsdf> {
    void operator()(Mbsdf& res) {
        for(size_t i = 0; i < 2; ++i) {
            if(res.has_data[i] != 0u) {
                checkCudaError(cuTexObjectDestroy(res.eval_data[i]));
                checkCudaError(cuMemFree(
                    reinterpret_cast<CUdeviceptr>(res.sample_data[i])));
                checkCudaError(cuMemFree(
                    reinterpret_cast<CUdeviceptr>(res.albedo_data[i])));
            }
        }
    }
};

template <>
struct Resource_deleter<Lightprofile> {
    void operator()(Lightprofile res) {
        if(res.cdf_data)
            checkCudaError(cuMemFree((CUdeviceptr)res.cdf_data));
    }
};

template <>
struct Resource_deleter<Target_code_data> {
    void operator()(Target_code_data& res) {
        if(res.textures)
            checkCudaError(cuMemFree(res.textures));
        if(res.ro_data_segment)
            checkCudaError(cuMemFree(res.ro_data_segment));
    }
};

template <>
struct Resource_deleter<CUdeviceptr> {
    void operator()(CUdeviceptr res) {
        if(res != 0)
            checkCudaError(cuMemFree(res));
    }
};

/// Holds one resource, not copyable.
template <typename T, typename D = Resource_deleter<T>>
struct Resource_handle {
    Resource_handle(T res) : m_res(res) {}

    ~Resource_handle() {
        D deleter;
        deleter(m_res);
    }

    T& get() {
        return m_res;
    }

    T const& get() const {
        return m_res;
    }

    void set(T res) {
        m_res = res;
    }

private:
    // No copy possible.
    Resource_handle(Resource_handle const&);
    Resource_handle& operator=(Resource_handle const&);

private:
    T m_res;
};

/// Hold one container of resources, not copyable.
template <typename T, typename C = std::vector<T>,
          typename D = Resource_deleter<T>>
struct Resource_container {
    Resource_container() : m_cont() {}

    ~Resource_container() {
        D deleter;
        typedef typename C::iterator I;
        for(I it(m_cont.begin()), end(m_cont.end()); it != end; ++it) {
            T& r = *it;
            deleter(r);
        }
    }

    C& operator*() {
        return m_cont;
    }

    C const& operator*() const {
        return m_cont;
    }

    C* operator->() {
        return &m_cont;
    }

    C const* operator->() const {
        return &m_cont;
    }

private:
    // No copy possible.
    Resource_container(Resource_container const&);
    Resource_container& operator=(Resource_container const&);

private:
    C m_cont;
};

// Allocate memory on GPU and copy the given data to the allocated memory.
CUdeviceptr gpu_mem_dup(void const* data, size_t size) {
    CUdeviceptr device_ptr;
    checkCudaError(cuMemAlloc(&device_ptr, size));
    checkCudaError(cuMemcpyHtoD(device_ptr, data, size));
    return device_ptr;
}

// Allocate memory on GPU and copy the given data to the allocated memory.
template <typename T>
CUdeviceptr gpu_mem_dup(Resource_handle<T> const* data, size_t size) {
    return gpu_mem_dup((void*)data->get(), size);
}

// Allocate memory on GPU and copy the given data to the allocated memory.
template <typename T>
CUdeviceptr gpu_mem_dup(std::vector<T> const& data) {
    return gpu_mem_dup(&data[0], data.size() * sizeof(T));
}

// Allocate memory on GPU and copy the given data to the allocated memory.
template <typename T, typename C>
CUdeviceptr gpu_mem_dup(Resource_container<T, C> const& cont) {
    return gpu_mem_dup(*cont);
}

//------------------------------------------------------------------------------
//
// Material_gpu_context class
//
//------------------------------------------------------------------------------

// Helper class responsible for making textures and read-only data available to
// the GPU by generating and managing a list of Target_code_data objects.
class Material_gpu_context {
public:
    Material_gpu_context(bool enable_derivatives)
        : m_enable_derivatives(enable_derivatives),
          m_device_target_code_data_list(0),
          m_device_target_argument_block_list(0) {
        // Use first entry as "not-used" block
        m_target_argument_block_list->push_back(0);
    }

    // Prepare the needed data of the given target code.
    void prepare_target_code_data(MDL::ITransaction* transaction,
                                  MDL::IImage_api* image_api,
                                  MDL::ITarget_code const* target_code,
                                  std::vector<size_t> const& arg_block_indices);

    // Get a device pointer to the target code data list.
    CUdeviceptr get_device_target_code_data_list();

    // Get a device pointer to the target argument block list.
    CUdeviceptr get_device_target_argument_block_list();

    // Get a device pointer to the i'th target argument block.
    CUdeviceptr get_device_target_argument_block(size_t i) {
        // First entry is the "not-used" block, so start at index 1.
        if(i + 1 >= m_target_argument_block_list->size())
            return 0;
        return (*m_target_argument_block_list)[i + 1];
    }

    // Get the number of target argument blocks.
    size_t get_argument_block_count() const {
        return m_own_arg_blocks.size();
    }

    // Get the argument block of the i'th BSDF.
    // If the BSDF has no target argument block, size_t(~0) is returned.
    size_t get_bsdf_argument_block_index(size_t i) const {
        if(i >= m_bsdf_arg_block_indices.size())
            return size_t(~0);
        return m_bsdf_arg_block_indices[i];
    }

    // Get a writable copy of the i'th target argument block.
    Handle<MDL::ITarget_argument_block> get_argument_block(size_t i) {
        if(i >= m_own_arg_blocks.size())
            return Handle<MDL::ITarget_argument_block>();
        return m_own_arg_blocks[i];
    }

    // Get the layout of the i'th target argument block.
    Handle<MDL::ITarget_value_layout const>
    get_argument_block_layout(size_t i) {
        if(i >= m_arg_block_layouts.size())
            return Handle<MDL::ITarget_value_layout const>();
        return m_arg_block_layouts[i];
    }

    // Update the i'th target argument block on the device with the data from
    // the corresponding block returned by get_argument_block().
    void update_device_argument_block(size_t i);

private:
    // Copy the image data of a canvas to a CUDA array.
    void copy_canvas_to_cuda_array(CUarray device_array,
                                   MDL::ICanvas const* canvas);

    // Prepare the texture identified by the texture_index for use by the
    // texture access functions on the GPU.
    void prepare_texture(MDL::ITransaction* transaction,
                         MDL::IImage_api* image_api,
                         MDL::ITarget_code const* code_ptx,
                         mi::Size texture_index,
                         std::vector<Texture>& textures);

    // Prepare the mbsdf identified by the mbsdf_index for use by the bsdf
    // measurement access functions on the GPU.
    void prepare_mbsdf(MDL::ITransaction* transaction,
                       MDL::ITarget_code const* code_ptx, mi::Size mbsdf_index,
                       std::vector<Mbsdf>& mbsdfs);

    // Prepare the mbsdf identified by the mbsdf_index for use by the bsdf
    // measurement access functions on the GPU.
    void prepare_lightprofile(MDL::ITransaction* transaction,
                              MDL::ITarget_code const* code_ptx,
                              mi::Size lightprofile_index,
                              std::vector<Lightprofile>& lightprofiles);

    // If true, mipmaps will be generated for all 2D textures.
    bool m_enable_derivatives;

    // The device pointer of the target code data list.
    Resource_handle<CUdeviceptr> m_device_target_code_data_list;

    // List of all target code data objects owned by this context.
    Resource_container<Target_code_data> m_target_code_data_list;

    // The device pointer of the target argument block list.
    Resource_handle<CUdeviceptr> m_device_target_argument_block_list;

    // List of all target argument blocks owned by this context.
    Resource_container<CUdeviceptr> m_target_argument_block_list;

    // List of all local, writable copies of the target argument blocks.
    std::vector<Handle<MDL::ITarget_argument_block>> m_own_arg_blocks;

    // List of argument block indices per material BSDF.
    std::vector<size_t> m_bsdf_arg_block_indices;

    // List of all target argument block layouts.
    std::vector<Handle<MDL::ITarget_value_layout const>> m_arg_block_layouts;

    // List of all Texture objects owned by this context.
    Resource_container<Texture> m_all_textures;

    // List of all MBSDFs objects owned by this context.
    Resource_container<Mbsdf> m_all_mbsdfs;

    // List of all Light profiles objects owned by this context.
    Resource_container<Lightprofile> m_all_lightprofiles;

    // List of all CUDA arrays owned by this context.
    Resource_container<CUarray> m_all_texture_arrays;

    // List of all CUDA mipmapped arrays owned by this context.
    Resource_container<CUmipmappedArray> m_all_texture_mipmapped_arrays;
};

// Get a device pointer to the target code data list.
CUdeviceptr Material_gpu_context::get_device_target_code_data_list() {
    if(!m_device_target_code_data_list.get())
        m_device_target_code_data_list.set(
            gpu_mem_dup(m_target_code_data_list));
    return m_device_target_code_data_list.get();
}

// Get a device pointer to the target argument block list.
CUdeviceptr Material_gpu_context::get_device_target_argument_block_list() {
    if(!m_device_target_argument_block_list.get())
        m_device_target_argument_block_list.set(
            gpu_mem_dup(m_target_argument_block_list));
    return m_device_target_argument_block_list.get();
}

// TODO:asynchronous and RAII

// Copy the image data of a canvas to a CUDA array.
void Material_gpu_context::copy_canvas_to_cuda_array(
    CUarray device_array, MDL::ICanvas const* canvas) {
    BUS_TRACE_BEG() {
        Handle<const MDL::ITile> tile(canvas->get_tile(0, 0));
        mi::Float32 const* data =
            static_cast<mi::Float32 const*>(tile->get_data());
        CUDA_MEMCPY2D copyParam = {};
        copyParam.WidthInBytes = canvas->get_resolution_x() * 4 * sizeof(float);
        copyParam.Height = canvas->get_resolution_y();
        copyParam.srcHost = data;
        copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
        copyParam.dstArray = device_array;
        copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        checkCudaError(cuMemcpy2D(&copyParam));
    }
    BUS_TRACE_END();
}

// Prepare the texture identified by the texture_index for use by the texture
// access functions on the GPU.
void Material_gpu_context::prepare_texture(MDL::ITransaction* transaction,
                                           MDL::IImage_api* image_api,
                                           MDL::ITarget_code const* code_ptx,
                                           mi::Size texture_index,
                                           std::vector<Texture>& textures) {
    BUS_TRACE_BEG() {
        // Get access to the texture data by the texture database name from the
        // target code.
        Handle<const MDL::ITexture> texture(transaction->access<MDL::ITexture>(
            code_ptx->get_texture(texture_index)));
        Handle<const MDL::IImage> image(
            transaction->access<MDL::IImage>(texture->get_image()));
        Handle<const MDL::ICanvas> canvas(image->get_canvas());
        mi::Uint32 tex_width = canvas->get_resolution_x();
        mi::Uint32 tex_height = canvas->get_resolution_y();
        mi::Uint32 tex_layers = canvas->get_layers_size();
        char const* image_type = image->get_type();

        if(image->is_uvtile()) {
            BUS_TRACE_THROW(
                std::logic_error("Unimplemented feature:uvtile texture"));
        }

        if(canvas->get_tiles_size_x() != 1 || canvas->get_tiles_size_y() != 1) {
            BUS_TRACE_THROW(
                std::logic_error("Unimplemented feature:tiled images"));
        }

        // For simplicity, the texture access functions are only implemented for
        // float4 and gamma is pre-applied here (all images are converted to
        // linear space).

        // Convert to linear color space if necessary
        if(texture->get_effective_gamma() != 1.0f) {
            // Copy/convert to float4 canvas and adjust gamma from "effective
            // gamma" to 1.
            Handle<MDL::ICanvas> gamma_canvas(
                image_api->convert(canvas.get(), "Color"));
            gamma_canvas->set_gamma(texture->get_effective_gamma());
            image_api->adjust_gamma(gamma_canvas.get(), 1.0f);
            canvas = gamma_canvas;
        } else if(strcmp(image_type, "Color") != 0 &&
                  strcmp(image_type, "Float32<4>") != 0) {
            // Convert to expected format
            canvas = image_api->convert(canvas.get(), "Color");
        }

        BUS_TRACE_POINT();

        CUDA_RESOURCE_DESC res_desc = {};
        memset(&res_desc, 0, sizeof(res_desc));

        // Copy image data to GPU array depending on texture shape
        MDL::ITarget_code::Texture_shape texture_shape =
            code_ptx->get_texture_shape(texture_index);
        if(texture_shape == MDL::ITarget_code::Texture_shape_cube ||
           texture_shape == MDL::ITarget_code::Texture_shape_3d) {
            // Cubemap and 3D texture objects require 3D CUDA arrays

            if(texture_shape == MDL::ITarget_code::Texture_shape_cube &&
               tex_layers != 6) {
                BUS_TRACE_THROW(std::logic_error(
                    "Invalid number of layers (" + std::to_string(tex_layers) +
                    "), cubemaps must have 6 layers!"));
            }

            // Allocate a 3D array on the GPU
            CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = tex_width;
            arrayDesc.Height = tex_height;
            arrayDesc.Depth = tex_layers;
            arrayDesc.Flags =
                (texture_shape == MDL::ITarget_code::Texture_shape_cube ?
                     CUDA_ARRAY3D_CUBEMAP :
                     0);
            arrayDesc.Format = CU_AD_FORMAT_FLOAT;
            arrayDesc.NumChannels = 4;
            CUarray device_tex_array;
            checkCudaError(cuArray3DCreate(&device_tex_array, &arrayDesc));

            BUS_TRACE_POINT();

            // Prepare the memcpy parameter structure
            CUDA_MEMCPY3D copy_params = {};
            memset(&copy_params, 0, sizeof(copy_params));
            copy_params.dstArray = device_tex_array;
            copy_params.WidthInBytes = tex_width * 4 * sizeof(float);
            copy_params.Height = tex_height;
            copy_params.Depth = 1;
            copy_params.srcMemoryType = CU_MEMORYTYPE_HOST;
            copy_params.dstMemoryType = CU_MEMORYTYPE_ARRAY;

            // Copy the image data of all layers (the layers are not
            // consecutive in memory)
            for(mi::Uint32 layer = 0; layer < tex_layers; ++layer) {
                Handle<const MDL::ITile> tile(canvas->get_tile(0, 0, layer));
                float const* data = static_cast<float const*>(tile->get_data());

                /*
                copy_params.srcPtr = make_cudaPitchedPtr(
                    const_cast<float*>(data), tex_width * sizeof(float) * 4,
                    tex_width, tex_height);
                    */
                copy_params.srcHost = data;
                copy_params.srcPitch = tex_width * sizeof(float) * 4;
                copy_params.dstHeight = layer;

                checkCudaError(cuMemcpy3D(&copy_params));
            }

            res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
            res_desc.res.array.hArray = device_tex_array;

            m_all_texture_arrays->push_back(device_tex_array);
        } else if(m_enable_derivatives) {
            // mipmapped textures use CUDA mipmapped arrays
            mi::Uint32 num_levels = image->get_levels();
            CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = tex_width;
            arrayDesc.Height = tex_height;
            arrayDesc.Depth = 0;
            arrayDesc.Flags = 0;
            arrayDesc.Format = CU_AD_FORMAT_FLOAT;
            arrayDesc.NumChannels = 4;
            CUmipmappedArray device_tex_miparray;
            checkCudaError(cuMipmappedArrayCreate(&device_tex_miparray,
                                                  &arrayDesc, num_levels));
            BUS_TRACE_POINT();
            // create all mipmap levels and copy them to the CUDA arrays in the
            // mipmapped array
            Handle<mi::IArray> mipmaps(
                image_api->create_mipmaps(canvas.get(), 1.0f));

            for(mi::Uint32 level = 0; level < num_levels; ++level) {
                Handle<MDL::ICanvas const> level_canvas;
                if(level == 0)
                    level_canvas = canvas;
                else {
                    Handle<mi::IPointer> mipmap_ptr(
                        mipmaps->get_element<mi::IPointer>(level - 1));
                    level_canvas = mipmap_ptr->get_pointer<MDL::ICanvas>();
                }
                CUarray device_level_array;
                checkCudaError(cuMipmappedArrayGetLevel(
                    &device_level_array, device_tex_miparray, level));
                copy_canvas_to_cuda_array(device_level_array,
                                          level_canvas.get());
            }

            BUS_TRACE_POINT();

            res_desc.resType = CU_RESOURCE_TYPE_MIPMAPPED_ARRAY;
            res_desc.res.mipmap.hMipmappedArray = device_tex_miparray;

            m_all_texture_mipmapped_arrays->push_back(device_tex_miparray);
        } else {
            // 2D texture objects use CUDA arrays
            CUDA_ARRAY_DESCRIPTOR arrayDesc = {};
            arrayDesc.Width = tex_width;
            arrayDesc.Height = tex_height;
            arrayDesc.NumChannels = 4;
            arrayDesc.Format = CU_AD_FORMAT_FLOAT;
            CUarray device_tex_array;
            checkCudaError(cuArrayCreate(&device_tex_array, &arrayDesc));

            BUS_TRACE_POINT();

            copy_canvas_to_cuda_array(device_tex_array, canvas.get());

            res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
            res_desc.res.array.hArray = device_tex_array;

            m_all_texture_arrays->push_back(device_tex_array);
        }

        BUS_TRACE_POINT();

        // For cube maps we need clamped address mode to avoid artifacts in the
        // corners
        CUaddress_mode addr_mode =
            texture_shape == MDL::ITarget_code::Texture_shape_cube ?
            CU_TR_ADDRESS_MODE_CLAMP :
            CU_TR_ADDRESS_MODE_WRAP;

        // Create filtered texture object
        CUDA_TEXTURE_DESC tex_desc = {};
        memset(&tex_desc, 0, sizeof(tex_desc));
        tex_desc.addressMode[0] = addr_mode;
        tex_desc.addressMode[1] = addr_mode;
        tex_desc.addressMode[2] = addr_mode;
        tex_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
        // tex_desc.readMode = cudaReadModeElementType;
        // tex_desc.normalizedCoords = 1;
        tex_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;
        if(res_desc.resType == CU_RESOURCE_TYPE_MIPMAPPED_ARRAY) {
            tex_desc.mipmapFilterMode = CU_TR_FILTER_MODE_LINEAR;
            tex_desc.maxAnisotropy = 16;
            tex_desc.minMipmapLevelClamp = 0.f;
            tex_desc.maxMipmapLevelClamp = 1000.f;  // default value in OpenGL
        }

        CUtexObject tex_obj = 0;
        checkCudaError(
            cuTexObjectCreate(&tex_obj, &res_desc, &tex_desc, nullptr));

        BUS_TRACE_POINT();

        // Create unfiltered texture object if necessary (cube textures have no
        // texel functions)
        CUtexObject tex_obj_unfilt = 0;
        if(texture_shape != MDL::ITarget_code::Texture_shape_cube) {
            // Use a black border for access outside of the texture
            tex_desc.addressMode[0] = CU_TR_ADDRESS_MODE_BORDER;
            tex_desc.addressMode[1] = CU_TR_ADDRESS_MODE_BORDER;
            tex_desc.addressMode[2] = CU_TR_ADDRESS_MODE_BORDER;
            tex_desc.filterMode = CU_TR_FILTER_MODE_POINT;

            checkCudaError(cuTexObjectCreate(&tex_obj_unfilt, &res_desc,
                                             &tex_desc, nullptr));
        }

        BUS_TRACE_POINT();

        // Store texture infos in result vector
        textures.push_back(Texture(tex_obj, tex_obj_unfilt,
                                   { tex_width, tex_height, tex_layers }));
        m_all_textures->push_back(textures.back());
    }
    BUS_TRACE_END();
}

static bool
prepare_mbsdfs_part(MDL::Mbsdf_part part, Mbsdf& mbsdf_cuda_representation,
                    const MDL::IBsdf_measurement* bsdf_measurement) {
    Handle<const MDL::Bsdf_isotropic_data> dataset;
    switch(part) {
        case MDL::MBSDF_DATA_REFLECTION:
            dataset =
                bsdf_measurement->get_reflection<MDL::Bsdf_isotropic_data>();
            break;
        case MDL::MBSDF_DATA_TRANSMISSION:
            dataset =
                bsdf_measurement->get_transmission<MDL::Bsdf_isotropic_data>();
            break;
    }

    // no data, fine
    if(!dataset)
        return true;

    // get dimensions
    Uint2 res;
    res.x = dataset->get_resolution_theta();
    res.y = dataset->get_resolution_phi();
    unsigned num_channels = dataset->get_type() == MDL::BSDF_SCALAR ? 1 : 3;
    mbsdf_cuda_representation.Add(part, res, num_channels);

    // get data
    Handle<const MDL::IBsdf_buffer> buffer(dataset->get_bsdf_buffer());
    // {1,3} * (index_theta_in * (res_phi * res_theta) + index_theta_out *
    // res_phi + index_phi)

    const mi::Float32* src_data = buffer->get_data();

    // ----------------------------------------------------------------------------------------
    // prepare importance sampling data:
    // - for theta_in we will be able to perform a two stage CDF, first to
    // select theta_out,
    //   and second to select phi_out
    // - maximum component is used to "probability" in case of colored
    // measurements

    // CDF of the probability to select a certain theta_out for a given
    // theta_in
    const unsigned int cdf_theta_size = res.x * res.x;

    // for each of theta_in x theta_out combination, a CDF of the
    // probabilities to select a a certain theta_out is stored
    const unsigned sample_data_size = cdf_theta_size + cdf_theta_size * res.y;
    float* sample_data = new float[sample_data_size];

    float* albedo_data = new float[res.x];  // albedo for sampling
                                            // reflection and transmission

    float* sample_data_theta = sample_data;  // begin of the first (theta) CDF
    float* sample_data_phi =
        sample_data + cdf_theta_size;  // begin of the second (phi) CDFs

    const float s_theta = (float)(M_PI * 0.5) / float(res.x);  // step size
    const float s_phi = (float)(M_PI) / float(res.y);          // step size

    float max_albedo = 0.0f;
    for(unsigned int t_in = 0; t_in < res.x; ++t_in) {
        float sum_theta = 0.0f;
        float sintheta0_sqd = 0.0f;
        for(unsigned int t_out = 0; t_out < res.x; ++t_out) {
            const float sintheta1 = sinf(float(t_out + 1) * s_theta);
            const float sintheta1_sqd = sintheta1 * sintheta1;

            // BSDFs are symmetric: f(w_in, w_out) = f(w_out, w_in)
            // take the average of both measurements

            // area of two the surface elements (the ones we are averaging)
            const float mu = (sintheta1_sqd - sintheta0_sqd) * s_phi * 0.5f;
            sintheta0_sqd = sintheta1_sqd;

            // offset for both the thetas into the measurement data (select
            // row in the volume)
            const unsigned int offset_phi = (t_in * res.x + t_out) * res.y;
            const unsigned int offset_phi2 = (t_out * res.x + t_in) * res.y;

            // build CDF for phi
            float sum_phi = 0.0f;
            for(unsigned int p_out = 0; p_out < res.y; ++p_out) {
                const unsigned int idx = offset_phi + p_out;
                const unsigned int idx2 = offset_phi2 + p_out;

                float value = 0.0f;
                if(num_channels == 3) {
                    value = fmax(fmaxf(src_data[3 * idx + 0],
                                       src_data[3 * idx + 1]),
                                 fmaxf(src_data[3 * idx + 2], 0.0f)) +
                        fmax(fmaxf(src_data[3 * idx2 + 0],
                                   src_data[3 * idx2 + 1]),
                             fmaxf(src_data[3 * idx2 + 2], 0.0f));
                } else /* num_channels == 1 */
                {
                    value = fmaxf(src_data[idx], 0.0f) +
                        fmaxf(src_data[idx2], 0.0f);
                }

                sum_phi += value * mu;
                sample_data_phi[idx] = sum_phi;
            }

            // normalize CDF for phi
            for(unsigned int p_out = 0; p_out < res.y; ++p_out) {
                const unsigned int idx = offset_phi + p_out;
                sample_data_phi[idx] = sample_data_phi[idx] / sum_phi;
            }

            // build CDF for theta
            sum_theta += sum_phi;
            sample_data_theta[t_in * res.x + t_out] = sum_theta;
        }

        if(sum_theta > max_albedo)
            max_albedo = sum_theta;

        albedo_data[t_in] = sum_theta;

        // normalize CDF for theta
        for(unsigned int t_out = 0; t_out < res.x; ++t_out) {
            const unsigned int idx = t_in * res.x + t_out;
            sample_data_theta[idx] = sample_data_theta[idx] / sum_theta;
        }
    }

    // copy entire CDF data buffer to GPU
    CUdeviceptr sample_obj = 0;
    checkCudaError(cuMemAlloc(&sample_obj, sample_data_size * sizeof(float)));
    checkCudaError(cuMemcpyHtoD(sample_obj, sample_data,
                                sample_data_size * sizeof(float)));
    delete[] sample_data;

    CUdeviceptr albedo_obj = 0;
    checkCudaError(cuMemAlloc(&albedo_obj, res.x * sizeof(float)));
    checkCudaError(
        cuMemcpyHtoD(albedo_obj, albedo_data, res.x * sizeof(float)));
    delete[] albedo_data;

    mbsdf_cuda_representation.sample_data[part] =
        reinterpret_cast<float*>(sample_obj);
    mbsdf_cuda_representation.albedo_data[part] =
        reinterpret_cast<float*>(albedo_obj);
    mbsdf_cuda_representation.max_albedo[part] = max_albedo;

    // ----------------------------------------------------------------------------------------
    // prepare evaluation data:
    // - simply store the measured data in a volume texture
    // - in case of color data, we store each sample in a vector4 to get
    // texture support
    unsigned lookup_channels = (num_channels == 3) ? 4 : 1;

    // make lookup data symmetric
    float* lookup_data = new float[lookup_channels * res.y * res.x * res.x];
    for(unsigned int t_in = 0; t_in < res.x; ++t_in) {
        for(unsigned int t_out = 0; t_out < res.x; ++t_out) {
            const unsigned int offset_phi = (t_in * res.x + t_out) * res.y;
            const unsigned int offset_phi2 = (t_out * res.x + t_in) * res.y;
            for(unsigned int p_out = 0; p_out < res.y; ++p_out) {
                const unsigned int idx = offset_phi + p_out;
                const unsigned int idx2 = offset_phi2 + p_out;

                if(num_channels == 3) {
                    lookup_data[4 * idx + 0] =
                        (src_data[3 * idx + 0] + src_data[3 * idx2 + 0]) * 0.5f;
                    lookup_data[4 * idx + 1] =
                        (src_data[3 * idx + 1] + src_data[3 * idx2 + 1]) * 0.5f;
                    lookup_data[4 * idx + 2] =
                        (src_data[3 * idx + 2] + src_data[3 * idx2 + 2]) * 0.5f;
                    lookup_data[4 * idx + 3] = 1.0f;
                } else {
                    lookup_data[idx] = (src_data[idx] + src_data[idx2]) * 0.5f;
                }
            }
        }
    }

    // Copy data to GPU array
    CUarray device_mbsdf_data;
    CUDA_ARRAY3D_DESCRIPTOR arrayDesc = {};
    arrayDesc.Width = res.y;
    arrayDesc.Height = res.x;
    arrayDesc.Depth = res.x;
    arrayDesc.Format = CU_AD_FORMAT_FLOAT;
    arrayDesc.NumChannels = lookup_channels;

    // Allocate a 3D array on the GPU (phi_delta x theta_out x theta_in)
    // cudaExtent extent = make_cudaExtent(res.y, res.x, res.x);
    checkCudaError(cuArray3DCreate(&device_mbsdf_data, &arrayDesc));

    // prepare and copy
    CUDA_MEMCPY3D copy_params = {};
    memset(&copy_params, 0, sizeof(copy_params));
    copy_params.srcHost = lookup_data;
    copy_params.srcPitch = res.y * lookup_channels * sizeof(float);
    copy_params.srcMemoryType = CU_MEMORYTYPE_HOST;

    copy_params.dstArray = device_mbsdf_data;
    copy_params.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy_params.WidthInBytes = res.y * lookup_channels * sizeof(float);
    copy_params.Height = res.x;
    copy_params.Depth = res.x;
    checkCudaError(cuMemcpy3D(&copy_params));
    delete[] lookup_data;

    CUDA_RESOURCE_DESC texRes = {};
    memset(&texRes, 0, sizeof(texRes));
    texRes.resType = CU_RESOURCE_TYPE_ARRAY;
    texRes.res.array.hArray = device_mbsdf_data;

    CUDA_TEXTURE_DESC texDescr = {};
    memset(&texDescr, 0, sizeof(texDescr));
    texDescr.flags = CU_TRSF_NORMALIZED_COORDINATES;
    texDescr.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texDescr.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
    texDescr.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    texDescr.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;

    CUtexObject eval_tex_obj;
    checkCudaError(
        cuTexObjectCreate(&eval_tex_obj, &texRes, &texDescr, nullptr));
    mbsdf_cuda_representation.eval_data[part] = eval_tex_obj;

    return true;
}

void Material_gpu_context::prepare_mbsdf(MDL::ITransaction* transaction,
                                         MDL::ITarget_code const* code_ptx,
                                         mi::Size mbsdf_index,
                                         std::vector<Mbsdf>& mbsdfs) {
    BUS_TRACE_BEG() {
        // Get access to the texture data by the texture database name from the
        // target code.
        Handle<const MDL::IBsdf_measurement> mbsdf(
            transaction->access<MDL::IBsdf_measurement>(
                code_ptx->get_bsdf_measurement(mbsdf_index)));

        Mbsdf mbsdf_cuda;

        // handle reflection and transmission
        prepare_mbsdfs_part(MDL::MBSDF_DATA_REFLECTION, mbsdf_cuda,
                            mbsdf.get());
        prepare_mbsdfs_part(MDL::MBSDF_DATA_TRANSMISSION, mbsdf_cuda,
                            mbsdf.get());

        mbsdfs.push_back(mbsdf_cuda);
        m_all_mbsdfs->push_back(mbsdfs.back());
    }
    BUS_TRACE_END();
}

void Material_gpu_context::prepare_lightprofile(
    MDL::ITransaction* transaction, MDL::ITarget_code const* code_ptx,
    mi::Size lightprofile_index, std::vector<Lightprofile>& lightprofiles) {
    BUS_TRACE_BEG() {

        // Get access to the texture data by the texture database name from the
        // target code.
        Handle<const MDL::ILightprofile> lprof_nr(
            transaction->access<MDL::ILightprofile>(
                code_ptx->get_light_profile(lightprofile_index)));

        Uint2 res = { lprof_nr->get_resolution_theta(),
                      lprof_nr->get_resolution_phi() };
        Vec2 start = { lprof_nr->get_theta(0), lprof_nr->get_phi(0) };
        Vec2 delta = { lprof_nr->get_theta(1) - start.x,
                       lprof_nr->get_phi(1) - start.y };

        // phi-mayor: [res.x x res.y]
        const float* data = lprof_nr->get_data();

        // --------------------------------------------------------------------------------------------
        // compute total power
        // compute inverse CDF data for sampling
        // sampling will work on cells rather than grid nodes (used for
        // evaluation)

        // first (res.x-1) for the cdf for sampling theta
        // rest (rex.x-1) * (res.y-1) for the individual cdfs for sampling phi
        // (after theta)
        size_t cdf_data_size = (res.x - 1) + (res.x - 1) * (res.y - 1);
        float* cdf_data = new float[cdf_data_size];

        float debug_total_erea = 0.0f;
        float sum_theta = 0.0f;
        float total_power = 0.0f;
        float cos_theta0 = cosf(start.x);
        for(unsigned int t = 0; t < res.x - 1; ++t) {
            const float cos_theta1 = cosf(start.x + float(t + 1) * delta.x);

            // area of the patch (grid cell)
            // \mu = int_{theta0}^{theta1} sin{theta} \delta theta
            const float mu = cos_theta0 - cos_theta1;
            cos_theta0 = cos_theta1;

            // build CDF for phi
            float* cdf_data_phi = cdf_data + (res.x - 1) + t * (res.y - 1);
            float sum_phi = 0.0f;
            for(unsigned int p = 0; p < res.y - 1; ++p) {
                // the probability to select a patch corresponds to the value
                // times area the value of a cell is the average of the corners
                // omit the *1/4 as we normalize in the end
                float value = data[p * res.x + t] + data[p * res.x + t + 1] +
                    data[(p + 1) * res.x + t] + data[(p + 1) * res.x + t + 1];

                sum_phi += value * mu;
                cdf_data_phi[p] = sum_phi;
                debug_total_erea += mu;
            }

            // normalize CDF for phi
            for(unsigned int p = 0; p < res.y - 2; ++p)
                cdf_data_phi[p] = sum_phi ? (cdf_data_phi[p] / sum_phi) : 0.0f;

            cdf_data_phi[res.y - 2] = 1.0f;

            // build CDF for theta
            sum_theta += sum_phi;
            cdf_data[t] = sum_theta;
        }
        total_power = sum_theta * 0.25f * delta.y;

        // normalize CDF for theta
        for(unsigned int t = 0; t < res.x - 2; ++t)
            cdf_data[t] = sum_theta ? (cdf_data[t] / sum_theta) : cdf_data[t];

        cdf_data[res.x - 2] = 1.0f;

        // copy entire CDF data buffer to GPU
        CUdeviceptr cdf_data_obj = 0;
        checkCudaError(
            cuMemAlloc(&cdf_data_obj, cdf_data_size * sizeof(float)));
        checkCudaError(cuMemcpyHtoD(cdf_data_obj, cdf_data,
                                    cdf_data_size * sizeof(float)));
        delete[] cdf_data;

        // --------------------------------------------------------------------------------------------
        // prepare evaluation data
        //  - use a 2d texture that allows bilinear interpolation
        // Copy data to GPU array
        CUarray device_lightprofile_data;
        // cudaChannelFormatDesc channel_desc = cudaCreateChannelDesc<float>();

        // 2D texture objects use CUDA arrays
        CUDA_ARRAY_DESCRIPTOR arrayDesc = {};
        arrayDesc.Width = res.x;
        arrayDesc.Height = res.y;
        arrayDesc.Format = CU_AD_FORMAT_FLOAT;
        arrayDesc.NumChannels = 1;
        checkCudaError(cuArrayCreate(&device_lightprofile_data, &arrayDesc));
        CUDA_MEMCPY2D copyParam = {};
        copyParam.WidthInBytes = res.x * sizeof(float);
        copyParam.Height = res.y;
        copyParam.srcHost = data;
        copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
        copyParam.dstArray = device_lightprofile_data;
        copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;

        checkCudaError(cuMemcpy2D(&copyParam));

        // Create filtered texture object
        CUDA_RESOURCE_DESC res_desc = {};
        memset(&res_desc, 0, sizeof(res_desc));
        res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
        res_desc.res.array.hArray = device_lightprofile_data;

        CUDA_TEXTURE_DESC tex_desc = {};
        memset(&tex_desc, 0, sizeof(tex_desc));
        tex_desc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
        tex_desc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
        tex_desc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;
        tex_desc.borderColor[0] = 1.0f;
        tex_desc.borderColor[1] = 1.0f;
        tex_desc.borderColor[2] = 1.0f;
        tex_desc.borderColor[3] = 1.0f;
        tex_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
        tex_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;

        CUtexObject tex_obj = 0;
        checkCudaError(
            cuTexObjectCreate(&tex_obj, &res_desc, &tex_desc, nullptr));

        double multiplier = lprof_nr->get_candela_multiplier();
        Lightprofile lprof(res, start, delta, float(multiplier),
                           float(total_power * multiplier), tex_obj,
                           reinterpret_cast<float*>(cdf_data_obj));

        lightprofiles.push_back(lprof);
        m_all_lightprofiles->push_back(lightprofiles.back());
    }
    BUS_TRACE_END();
}

// Prepare the needed target code data of the given target code.
void Material_gpu_context::prepare_target_code_data(
    MDL::ITransaction* transaction, MDL::IImage_api* image_api,
    MDL::ITarget_code const* target_code,
    std::vector<size_t> const& arg_block_indices) {
    BUS_TRACE_BEG() {
        // Target code data list may not have been retrieved already
        check_success(m_device_target_code_data_list.get() == 0);

        // Handle the read-only data segments if necessary.
        // They are only created, if the "enable_ro_segment" backend option was
        // set to "on".
        CUdeviceptr device_ro_data = 0;
        if(target_code->get_ro_data_segment_count() > 0) {
            device_ro_data =
                gpu_mem_dup(target_code->get_ro_data_segment_data(0),
                            target_code->get_ro_data_segment_size(0));
        }

        // Copy textures to GPU if the code has more than just the invalid
        // texture
        CUdeviceptr device_textures = 0;
        mi::Size num_textures = target_code->get_texture_count();
        if(num_textures > 1) {
            std::vector<Texture> textures;

            // Loop over all textures skipping the first texture,
            // which is always the invalid texture
            for(mi::Size i = 1; i < num_textures; ++i) {
                prepare_texture(transaction, image_api, target_code, i,
                                textures);
            }

            // Copy texture list to GPU
            device_textures = gpu_mem_dup(textures);
        }

        // Copy MBSDFs to GPU if the code has more than just the invalid mbsdf
        CUdeviceptr device_mbsdfs = 0;
        mi::Size num_mbsdfs = target_code->get_bsdf_measurement_count();
        if(num_mbsdfs > 1) {
            std::vector<Mbsdf> mbsdfs;

            // Loop over all mbsdfs skipping the first mbsdf,
            // which is always the invalid mbsdf
            for(mi::Size i = 1; i < num_mbsdfs; ++i) {
                prepare_mbsdf(transaction, target_code, i, mbsdfs);
            }

            // Copy mbsdf list to GPU
            device_mbsdfs = gpu_mem_dup(mbsdfs);
        }

        // Copy light profiles to GPU if the code has more than just the invalid
        // light profile
        CUdeviceptr device_lightprofiles = 0;
        mi::Size num_lightprofiles = target_code->get_light_profile_count();
        if(num_lightprofiles > 1) {
            std::vector<Lightprofile> lightprofiles;

            // Loop over all profiles skipping the first profile,
            // which is always the invalid profile
            for(mi::Size i = 1; i < num_lightprofiles; ++i) {
                prepare_lightprofile(transaction, target_code, i,
                                     lightprofiles);
            }

            // Copy light profile list to GPU
            device_lightprofiles = gpu_mem_dup(lightprofiles);
        }

        (*m_target_code_data_list)
            .push_back(Target_code_data(
                num_textures, device_textures, num_mbsdfs, device_mbsdfs,
                num_lightprofiles, device_lightprofiles, device_ro_data));

        for(mi::Size i = 0, num = target_code->get_argument_block_count();
            i < num; ++i) {
            Handle<MDL::ITarget_argument_block const> arg_block(
                target_code->get_argument_block(i));
            CUdeviceptr dev_block =
                gpu_mem_dup(arg_block->get_data(), arg_block->get_size());
            m_target_argument_block_list->push_back(dev_block);
            m_own_arg_blocks.push_back(
                mi::base::make_handle(arg_block->clone()));
            m_arg_block_layouts.push_back(mi::base::make_handle(
                target_code->get_argument_block_layout(i)));
        }

        for(size_t arg_block_index : arg_block_indices) {
            m_bsdf_arg_block_indices.push_back(arg_block_index);
        }
    }
    BUS_TRACE_END();
}

// Update the i'th target argument block on the device with the data from the
// corresponding block returned by get_argument_block().
void Material_gpu_context::update_device_argument_block(size_t i) {
    CUdeviceptr device_ptr = get_device_target_argument_block(i);
    if(device_ptr == 0)
        return;

    Handle<MDL::ITarget_argument_block> arg_block(get_argument_block(i));
    checkCudaError(
        cuMemcpyHtoD(device_ptr, arg_block->get_data(), arg_block->get_size()));
}

//------------------------------------------------------------------------------
//
// MDL material compilation code
//
//------------------------------------------------------------------------------

class Material_compiler {
public:
    // Constructor.
    Material_compiler(MDL::IMdl_compiler* mdl_compiler,
                      MDL::IMdl_factory* mdl_factory,
                      MDL::ITransaction* transaction,
                      unsigned num_texture_results, bool enable_derivatives,
                      bool fold_ternary_on_df);

    // Helper function that checks if the provided name describes an MDLe
    // element.
    static bool is_mdle_name(const std::string& name);

    // Helper function to extract the module name from a fully-qualified
    // material name or a fully-qualified MDLE material name.
    static std::string get_module_name(const std::string& material_name);

    // Helper function to extract the material name from a fully-qualified
    // material name or a fully-qualified MDLE material name.
    static std::string get_material_name(const std::string& material_name);

    // Return the list of all material names in the given MDL module.
    std::vector<std::string> get_material_names(const std::string& module_name);

    // Add a subexpression of a given material to the link unit.
    // path is the path of the sub-expression.
    // fname is the function name in the generated code.
    // If class_compilation is true, the material will use class compilation.
    bool add_material_subexpr(const std::string& material_name,
                              const char* path, const char* fname,
                              bool class_compilation = false);

    // Add a distribution function of a given material to the link unit.
    // path is the path of the sub-expression.
    // fname is the function name in the generated code.
    // If class_compilation is true, the material will use class compilation.
    bool add_material_df(const std::string& material_name, const char* path,
                         const char* base_fname,
                         bool class_compilation = false);

    // Add (multiple) MDL distribution function and expressions of a material to
    // this link unit. For each distribution function it results in four
    // functions, suffixed with \c "_init", \c "_sample", \c "_evaluate", and \c
    // "_pdf". Functions can be selected by providing a a list of \c
    // Target_function_descriptions. Each of them needs to define the \c path,
    // the root of the expression that should be translated. After calling this
    // function, each element of the list will contain information for later
    // usage in the application, e.g., the \c argument_block_index and the \c
    // function_index.
    bool add_material(const std::string& material_name,
                      MDL::Target_function_description* function_descriptions,
                      mi::Size description_count,
                      bool class_compilation = false);

    // Generates CUDA PTX target code for the current link unit.
    Handle<const MDL::ITarget_code> generate_cuda_ptx();

    typedef std::vector<Handle<MDL::IMaterial_definition const>>
        Material_definition_list;

    // Get the list of used material definitions.
    // There will be one entry per add_* call.
    Material_definition_list const& get_material_defs() {
        return m_material_defs;
    }

    typedef std::vector<Handle<MDL::ICompiled_material const>>
        Compiled_material_list;

    // Get the list of compiled materials.
    // There will be one entry per add_* call.
    Compiled_material_list const& get_compiled_materials() {
        return m_compiled_materials;
    }

    /// Get the list of argument block indices per material.
    std::vector<size_t> const& get_argument_block_indices() const {
        return m_arg_block_indexes;
    }

private:
    // Creates an instance of the given material.
    MDL::IMaterial_instance*
    create_material_instance(const std::string& material_name);

    // Compiles the given material instance in the given compilation modes.
    MDL::ICompiled_material*
    compile_material_instance(MDL::IMaterial_instance* material_instance,
                              bool class_compilation);

private:
    Handle<MDL::IMdl_compiler> m_mdl_compiler;
    Handle<MDL::IMdl_backend> m_be_cuda_ptx;
    Handle<MDL::ITransaction> m_transaction;

    Handle<MDL::IMdl_execution_context> m_context;
    Handle<MDL::ILink_unit> m_link_unit;

    Material_definition_list m_material_defs;
    Compiled_material_list m_compiled_materials;
    std::vector<size_t> m_arg_block_indexes;
};

// Constructor.
Material_compiler::Material_compiler(MDL::IMdl_compiler* mdl_compiler,
                                     MDL::IMdl_factory* mdl_factory,
                                     MDL::ITransaction* transaction,
                                     unsigned num_texture_results,
                                     bool enable_derivatives,
                                     bool fold_ternary_on_df)
    : m_mdl_compiler(mi::base::make_handle_dup(mdl_compiler)),
      m_be_cuda_ptx(mdl_compiler->get_backend(MDL::IMdl_compiler::MB_CUDA_PTX)),
      m_transaction(mi::base::make_handle_dup(transaction)),
      m_context(mdl_factory->create_execution_context()), m_link_unit() {
    BUS_TRACE_BEG() {
        check_success(m_be_cuda_ptx->set_option("num_texture_spaces", "1") ==
                      0);

        // TODO:enable_ro_segment
        // Option "enable_ro_segment": Default is disabled.
        // If you have a lot of big arrays, enabling this might speed up
        // compilation.
        // check_success(m_be_cuda_ptx->set_option("enable_ro_segment", "on") ==
        // 0);

        if(enable_derivatives) {
            // Option "texture_runtime_with_derivs": Default is disabled.
            // We enable it to get coordinates with derivatives for texture
            // lookup functions.
            check_success(m_be_cuda_ptx->set_option(
                              "texture_runtime_with_derivs", "on") == 0);
        }

        check_success(m_be_cuda_ptx->set_option("tex_lookup_call_mode",
                                                "direct_call") == 0);

        // Option "num_texture_results": Default is 0.
        // Set the size of a renderer provided array for texture results in the
        // MDL SDK state in number of float4 elements processed by the init()
        // function.
        check_success(m_be_cuda_ptx->set_option(
                          "num_texture_results",
                          to_string(num_texture_results).c_str()) == 0);

        // force experimental to true for now
        m_context->set_option("experimental", true);

        m_context->set_option("fold_ternary_on_df", fold_ternary_on_df);

        // After we set the options, we can create the link unit
        m_link_unit = mi::base::make_handle(
            m_be_cuda_ptx->create_link_unit(transaction, m_context.get()));
    }
    BUS_TRACE_END();
}

bool Material_compiler::is_mdle_name(const std::string& name) {
    size_t l = name.length();
    if(l > 5 && name[l - 5] == '.' && name[l - 4] == 'm' &&
       name[l - 3] == 'd' && name[l - 2] == 'l' && name[l - 1] == 'e')
        return true;

    return name.find(".mdle:") != std::string::npos;
}

// Helper function to extract the module name from a fully-qualified material
// name.
std::string
Material_compiler::get_module_name(const std::string& material_name) {
    std::string module_name = material_name;

    if(is_mdle_name(module_name)) {
        // for MDLE, the module name is not supposed to have a leading "::",
        // strip it gracefully.
        if(module_name[0] == ':' && module_name[1] == ':')
            module_name = module_name.substr(2);
        std::replace(module_name.begin(), module_name.end(), '\\', '/');
    }

    // strip away the material name
    size_t p = module_name.rfind("::");
    if(p != std::string::npos)
        module_name = module_name.substr(0, p);

    return module_name;
}

// Helper function to extract the material name from a fully-qualified material
// name.
std::string
Material_compiler::get_material_name(const std::string& material_name) {
    size_t p = material_name.rfind("::");
    if(p == std::string::npos)
        return material_name;
    return material_name.substr(p + 2, material_name.size() - p);
}

// Return the list of all material names in the given MDL module.
std::vector<std::string>
Material_compiler::get_material_names(const std::string& module_name) {
    BUS_TRACE_BEG() {
        check_success(!is_mdle_name(module_name));
        check_success(m_mdl_compiler->load_module(m_transaction.get(),
                                                  module_name.c_str()) >= 0);

        const char* prefix = (module_name.find("::") == 0) ? "mdl" : "mdl::";

        Handle<const MDL::IModule> module(m_transaction->access<MDL::IModule>(
            (prefix + module_name).c_str()));

        mi::Size num_materials = module->get_material_count();
        std::vector<std::string> material_names(num_materials);
        for(mi::Size i = 0; i < num_materials; ++i) {
            material_names[i] = module->get_material(i);
        }
        return material_names;
    }
    BUS_TRACE_END();
}

// Creates an instance of the given material.
MDL::IMaterial_instance*
Material_compiler::create_material_instance(const std::string& material_name) {
    BUS_TRACE_BEG() {
        std::string module_name;
        std::string function_name;

        if(is_mdle_name(material_name)) {
            module_name = material_name;
            function_name = "main";
        } else {
            // strip away the material name
            size_t p = material_name.rfind("::");
            check_success(p != std::string::npos && p != 0 &&
                          "provided material name is invalid");
            module_name = material_name.substr(0, p);
            function_name = material_name.substr(p + 2);
        }

        // Load mdl module.
        check_success(m_mdl_compiler->load_module(m_transaction.get(),
                                                  module_name.c_str(),
                                                  m_context.get()) >= 0);

        // get db name
        const char* module_db_name = m_mdl_compiler->get_module_db_name(
            m_transaction.get(), module_name.c_str(), m_context.get());

        std::string material_db_name =
            std::string(module_db_name) + "::" + function_name;

        // Create a material instance from the material definition
        // with the default arguments.
        Handle<const MDL::IMaterial_definition> material_definition(
            m_transaction->access<MDL::IMaterial_definition>(
                material_db_name.c_str()));
        check_success(material_definition);

        m_material_defs.push_back(material_definition);

        mi::Sint32 result;
        Handle<MDL::IMaterial_instance> material_instance(
            material_definition->create_material_instance(0, &result));
        check_success(result == 0);

        material_instance->retain();
        return material_instance.get();
    }
    BUS_TRACE_END();
}

// Compiles the given material instance in the given compilation modes.
MDL::ICompiled_material* Material_compiler::compile_material_instance(
    MDL::IMaterial_instance* material_instance, bool class_compilation) {
    BUS_TRACE_BEG() {
        mi::Uint32 flags = class_compilation ?
            MDL::IMaterial_instance::CLASS_COMPILATION :
            MDL::IMaterial_instance::DEFAULT_OPTIONS;
        Handle<MDL::ICompiled_material> compiled_material(
            material_instance->create_compiled_material(flags,
                                                        m_context.get()));
        check_success(m_context->get_error_messages_count() == 0);

        m_compiled_materials.push_back(compiled_material);

        compiled_material->retain();
        return compiled_material.get();
    }
    BUS_TRACE_END();
}

// Generates CUDA PTX target code for the current link unit.
Handle<const MDL::ITarget_code> Material_compiler::generate_cuda_ptx() {
    BUS_TRACE_BEG() {
        Handle<const MDL::ITarget_code> code_cuda_ptx(
            m_be_cuda_ptx->translate_link_unit(m_link_unit.get(),
                                               m_context.get()));
        check_success(m_context->get_error_messages_count() == 0);
        check_success(code_cuda_ptx);
        return code_cuda_ptx;
    }
    BUS_TRACE_END();
}

// Add a subexpression of a given material to the link unit.
// path is the path of the sub-expression.
// fname is the function name in the generated code.
bool Material_compiler::add_material_subexpr(const std::string& material_name,
                                             const char* path,
                                             const char* fname,
                                             bool class_compilation) {
    MDL::Target_function_description desc;
    desc.path = path;
    desc.base_fname = fname;
    add_material(material_name, &desc, 1, class_compilation);
    return desc.return_code == 0;
}

// Add a distribution function of a given material to the link unit.
// path is the path of the sub-expression.
// fname is the function name in the generated code.
bool Material_compiler::add_material_df(const std::string& material_name,
                                        const char* path,
                                        const char* base_fname,
                                        bool class_compilation) {
    MDL::Target_function_description desc;
    desc.path = path;
    desc.base_fname = base_fname;
    add_material(material_name, &desc, 1, class_compilation);
    return desc.return_code == 0;
}

// Add (multiple) MDL distribution function and expressions of a material to
// this link unit. For each distribution function it results in four functions,
// suffixed with \c "_init", \c "_sample", \c "_evaluate", and \c "_pdf".
// Functions can be selected by providing a a list of \c
// Target_function_description. Each of them needs to define the \c path, the
// root of the expression that should be translated. After calling this
// function, each element of the list will contain information for later usage
// in the application, e.g., the \c argument_block_index and the \c
// function_index.
bool Material_compiler::add_material(
    const std::string& material_name,
    MDL::Target_function_description* function_descriptions,
    mi::Size description_count, bool class_compilation) {
    if(description_count == 0)
        return false;

    // Load the given module and create a material instance
    Handle<MDL::IMaterial_instance> material_instance(
        create_material_instance(material_name.c_str()));

    // Compile the material instance in instance compilation mode
    Handle<MDL::ICompiled_material> compiled_material(
        compile_material_instance(material_instance.get(), class_compilation));

    m_link_unit->add_material(compiled_material.get(), function_descriptions,
                              description_count, m_context.get());

    // Note: the same argument_block_index is filled into all function
    // descriptions of a
    //       material, if any function uses it
    m_arg_block_indexes.push_back(
        function_descriptions[0].argument_block_index);

    return m_context->get_error_messages_count() == 0;
}

#include "DataDesc.hpp"

class MDLCUDAHelper : private Unmoveable {
public:
    virtual std::string genPTX() = 0;
    virtual DataDesc getData() = 0;
    virtual mi::Uint32 getUsage() = 0;
};

class MDLCUDAHelperImpl : public MDLCUDAHelper {
private:
    Material_compiler mCompiler;
    Material_gpu_context mContext;
    Handle<const MDL::ITarget_code> mCode;

public:
    MDLCUDAHelperImpl(Context& context, const std::string& module,
                      const std::string& mat)
        // TODO:num_texture_results,  enable_derivatives, fold_ternary_on_df
        : mCompiler(context.compiler, context.factory, context.transaction, 0,
                    false, false),
          mContext(false) {
        BUS_TRACE_BEG() {
            check_success(
                mCompiler.add_material_df(mat, "surface.scattering", "bsdf"));
            mCode = mCompiler.generate_cuda_ptx();
            check_success(mCode.is_valid_interface());
            Handle<MDL::IImage_api> image_api(
                context.neuary->get_api_component<MDL::IImage_api>());
            std::vector<size_t> data;
            mi::Size argID =
                mCode->get_callable_function_argument_block_index(0);
            if(argID != ~mi::Size(0))
                data.push_back(argID);
            mContext.prepare_target_code_data(
                context.transaction, image_api.get(), mCode.get(), data);
        }
        BUS_TRACE_END();
    }
    std::string genPTX() override {
        return mCode->get_code();
    }
    DataDesc getData() override {
        DataDesc res;
        // TODO:Context Singleton
        res.argData = reinterpret_cast<char*>(
            mContext.get_device_target_argument_block(0));
        res.resource = mContext.get_device_target_code_data_list();
        return res;
    }
    mi::Uint32 getUsage() override {
        return mCode->get_render_state_usage();
    }
};

std::shared_ptr<MDLCUDAHelper>
getHelper(Context& context, const std::string& module, const std::string& mat) {
    return std::make_shared<MDLCUDAHelperImpl>(context, module, mat);
}
