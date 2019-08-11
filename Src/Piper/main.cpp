#pragma warning(push, 0)
#include <cxxopts.hpp>
#pragma warning(pop)
#include <filesystem>
#include <fstream>
#include "SceneLoader.hpp"
#include "../Shared.hpp"

namespace fs = std::experimental::filesystem;

optix::Context createContext(JsonHelper config) {
    int RTXMode = config->toBool("RTXMode");
    RTresult res = rtGlobalSetAttribute(RT_GLOBAL_ATTRIBUTE_ENABLE_RTX, 
        sizeof(int), &RTXMode);
    optix::Context context = optix::Context::create();
    context->checkErrorNoGetContext(res);
    return context;
}

bool render(const fs::path &in) {
    if (fs::exists(in)) {
        Json scene = loadScene(in);
        JsonHelper config = buildJsonHelper(scene);
        optix::Context context = createContext(config->attribute("Global"));
        std::string runtimeLib = config->attribute("Global")->toString("RuntimeLib");
        PluginHelper helper = buildPluginHelper(context, fs::current_path() / "Libs" /
            runtimeLib, in.parent_path());

        return true;
    }
    ERROR << (in.empty() ?
        "Please specify a scene file." :
        "Invalid scene file name.");
    return false;
}

int main(int argc, char **argv) {
    cxxopts::Options opt("Piper", "Piper-Main");
    opt.add_options()(
        "i,input", "scene desc path",
        cxxopts::value<fs::path>()->default_value(
        "testScene.json"));
    auto optRes = opt.parse(argc, argv);
    auto scene = optRes["input"].as<fs::path>();
    if (!render(scene))
        std::cout << opt.help() << std::endl;
    return 0;
}
