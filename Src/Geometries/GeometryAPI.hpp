#pragma once
#include "../PluginShared.hpp"

class Geometry : public PluginSharedAPI {
public:
    static std::string pluginInterface() {
        return "Piper.Geometry:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Geometries" };
    }

    explicit Geometry(PM::AbstractManager &manager,
        const std::string &plugin)
        : PluginSharedAPI{ manager, plugin } {}

    virtual void init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) = 0;
    virtual optix::Geometry getGeometry() = 0;
};
