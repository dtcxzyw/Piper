#pragma once
#include "../PluginShared.hpp"

struct LightProgram final {
    rtCallableProgramId<LightSample(const Mat4 &,
        rtBufferId<char, 1>, const Vec3 &)> prog;
    rtBufferId<char, 1> buf;
};

class Light : public PM::AbstractPlugin {
public:
    static std::string pluginInterface() {
        return "Piper.Light:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Lights" };
    }

    explicit Light(PM::AbstractManager &manager,
        const std::string &plugin)
        : AbstractPlugin{ manager, plugin } {}

    virtual LightProgram init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) = 0;
};
