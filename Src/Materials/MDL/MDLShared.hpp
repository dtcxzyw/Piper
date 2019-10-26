#pragma once
#pragma warning(push, 0)
#include "../../ThirdParty/Bus/BusReporter.hpp"
#include <mi/mdl_sdk.h>
#pragma warning(pop)
#include <filesystem>
#include <string>
#include <vector>

namespace MDL = mi::neuraylib;
using mi::base::Handle;
namespace fs = std::experimental::filesystem;

#define checkMDLError(expr)                               \
    do {                                                  \
        if(!(expr)) {                                     \
            const char* msg = "MDL error \"" #expr "\"."; \
            BUS_TRACE_THROW(std::runtime_error(msg));     \
        }                                                 \
    } while(false)

void configure(MDL::INeuray* neuray,
               const std::vector<std::string>& searchPath);
bool printMessages(Bus::Reporter& reporter,
                   MDL::IMdl_execution_context* context);
MDL::INeuray* MDLInit(HMODULE& handle, const fs::path& path,
                      Bus::Reporter& reporter);
void MDLUninit(HMODULE handle, Bus::Reporter& reporter);
