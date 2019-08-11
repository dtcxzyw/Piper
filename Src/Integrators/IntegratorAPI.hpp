#pragma once
#include "../PluginShared.hpp"

class Integrator : public PM::AbstractPlugin {
public:
    static std::string pluginInterface() {
        return "Piper.Integrator:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Integrators" };
    }

    explicit Integrator(PM::AbstractManager &manager,
        const std::string &plugin) : AbstractPlugin{ manager, plugin } {}

    virtual optix::Program init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath, const fs::path &cameraPTX) = 0;
};
