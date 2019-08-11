#pragma once
#include "../PluginShared.hpp"

class Material : public PM::AbstractPlugin {
public:
    static std::string pluginInterface() {
        return "Piper.Material:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Materials" };
    }

    explicit Material(PM::AbstractManager &manager,
        const std::string &plugin)
        : AbstractPlugin{ manager, plugin } {}

    virtual void init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) = 0;

    virtual optix::Material getMaterial() const = 0;
};
