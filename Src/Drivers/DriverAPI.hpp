#pragma once
#include "../PluginShared.hpp"

class Driver : public PM::AbstractPlugin {
public:
    static std::string pluginInterface() {
        return "Piper.Driver:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Drivers" };
    }

    explicit Driver(PM::AbstractManager &manager,
        const std::string &plugin)
        : AbstractPlugin{ manager, plugin } {}

    virtual uint2 init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) = 0;
    virtual void doRender() = 0;
};
