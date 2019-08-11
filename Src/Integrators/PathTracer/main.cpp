#define CORRADE_DYNAMIC_PLUGIN
#include "../IntegratorAPI.hpp"

class PathTracer final : public Integrator {
private:
    optix::Program mProgram;
public:
    explicit PathTracer(PM::AbstractManager &manager,
        const std::string &plugin) : Integrator{ manager, plugin } {}
    optix::Program init(PluginHelper helper, JsonHelper config, const fs::path &modulePath,
        const fs::path &cameraPTX) override {
        mProgram = helper->compile("traceKernel", { "PathKernel.ptx" }, modulePath,
            { cameraPTX });
        mProgram["integratorSample"]->setUint(config->toUint("Sample"));
        mProgram["integratorMaxDepth"]->setUint(config->toUint("MaxDepth"));
        return mProgram;
    }
};
CORRADE_PLUGIN_REGISTER(PathTracer, PathTracer, "Piper.Integrator:1")
