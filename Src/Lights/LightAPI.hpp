#pragma once
#include "../PluginShared.hpp"

struct LightProgram final {
    int prog, buf;
};

class Light : public PluginSharedAPI {
public:
    static std::string pluginInterface() {
        return "Piper.Light:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Lights" };
    }

    explicit Light(PM::AbstractManager &manager,
        const std::string &plugin)
        : PluginSharedAPI{ manager, plugin } {}

    virtual LightProgram init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) = 0;
    virtual optix::Program getProgram() = 0;
};
