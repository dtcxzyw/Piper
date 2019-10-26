#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/MaterialAPI.hpp"
#include "../../Shared/TextureSamplerAPI.hpp"
#pragma warning(push, 0)
#include "MDLShared.hpp"
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinMaterial.MDL");

class Instance final : public Bus::ModuleInstance {
private:
    HMODULE mHandle;
    Handle<MDL::INeuray> mNeuray;

public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {
        BUS_TRACE_BEG() {
            checkOptixError(optixInit());
            mNeuray = MDLInit(mHandle, path, sys.getReporter());
            if(!mNeuray)
                BUS_TRACE_THROW(
                    std::runtime_error("Failed to initialize MDL."));
            // TODO:config
            mi::Sint32 res = mNeuray->start(true);
            const char* errString = nullptr;
            switch(res) {
                case 0:
                    errString = nullptr;
                    break;
                case 1:
                    errString = "Unspecified failure.";
                    break;
                case 2:
                    errString = "Authentication failure(challenge-response).";
                    break;
                case 3:
                    errString = "Authentication failure (SPM).";
                    break;
                case 4:
                    errString = "Provided license expired.";
                    break;
                case 5:
                    errString =
                        "No professional GPU as required by the license in "
                        "use was found.";
                    break;
                case 6:
                    errString = "Authentication failure (FLEXlm).";
                    break;
                case 7:
                    errString =
                        "No NVIDIA VCA as required by the license in use "
                        "was found.";
                    break;
                default:
                    errString = "Unknown error.";
                    break;
            }
            if(errString)
                BUS_TRACE_THROW(std::runtime_error(
                    std::string("Failed to start MDL neuray:") + errString));
        }
        BUS_TRACE_END();
    }
    ~Instance() {
        if(mNeuray->shutdown(true) != 0)
            getSystem().getReporter().apply(
                ReportLevel::Error,
                "Failed to shutdown neuray: ", BUS_DEFSRCLOC());
        mNeuray.reset();
        MDLUninit(mHandle, getSystem().getReporter());
    }
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = BUS_DEFAULT_MODULE_NAME;
        res.guid = Bus::str2GUID("{36FBEACF-0545-4560-8CA2-997AF736D064}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "NVIDIA Material Definition Language";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    try {
        instance = std::make_shared<Instance>(path, system);
    } catch(...) {
        // TODO:Exceptions arisen in initialization are held by core.
        system.getReporter().apply(ReportLevel::Error,
                                   "An exception arose in initialization.",
                                   BUS_DEFSRCLOC());
    }
}
