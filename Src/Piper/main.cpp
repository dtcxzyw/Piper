#pragma warning(push, 0)
#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#pragma warning(pop)
#include <filesystem>
#include <fstream>
#include "../PluginShared.hpp"
#include "CameraAdapter.hpp"
#include "../Materials/MaterialAPI.hpp"
#include "../Lights/LightAPI.hpp"
#include "../Drivers/DriverAPI.hpp"
#include "../Integrators/IntegratorAPI.hpp"
#include "../Geometries/GeometryAPI.hpp"
#include <map>
#include <any>
#include <regex>
#include "../Shared.hpp"

namespace fs = std::experimental::filesystem;
using Json = nlohmann::json;

optix::Context createContext(JsonHelper config) {
    int RTXMode = config->toBool("RTXMode");
    RTresult res = rtGlobalSetAttribute(RT_GLOBAL_ATTRIBUTE_ENABLE_RTX,
        sizeof(int), &RTXMode);
    optix::Context context = optix::Context::create();
    context->checkErrorNoGetContext(res);
    context->setExceptionEnabled(RT_EXCEPTION_ALL, true);
    context->setEntryPointCount(1);
    context->setRayTypeCount(2);
    context->setDiskCacheLocation("Cache");
    context->setAttribute(RT_CONTEXT_ATTRIBUTE_DISK_CACHE_ENABLED, 1);
    context->setPrintEnabled(true);
    context->setPrintBufferSize(1048576);
    return context;
}

Json loadScene(const fs::path &path) {
    std::ifstream in(path);
    Json sceneData;
    try {
        in >> sceneData;
        return sceneData;
    }
    catch (const std::exception &ex) {
        FATAL << "Failed to load scene file :" << ex.what();
    }
}

using MaterialLib = std::map<std::string, Plugin<Material>>;

MaterialLib loadMaterials(JsonHelper config, PluginHelper helper) {
    MaterialLib res;
    std::vector<JsonHelper> mats = config->expand();
    for (auto cfg : mats) {
        std::string name = cfg->toString("Name");
        if (res.count(name))
            FATAL << "Material \"" << name << "\" has been defined.";
        else {
            std::string plugin = cfg->toString("Plugin");
            Plugin<Material> mat(plugin);
            mat->init(helper, cfg, fs::path("Plugins/Materials") / plugin);
            res.emplace(name, mat);
        }
    }
    return res;
}

using LightLib = std::map<std::string, LightProgram>;

LightLib loadLights(JsonHelper config, PluginHelper helper,
    std::vector<Plugin<Light>> &lig) {
    LightLib res;
    std::vector<JsonHelper> lights = config->expand();
    for (auto cfg : lights) {
        std::string name = cfg->toString("Name");
        if (res.count(name))
            FATAL << "Light \"" << name << "\" has been defined.";
        else {
            std::string plugin = cfg->toString("Plugin");
            Plugin<Light> light(plugin);
            res.emplace(name, light->init(helper, cfg, fs::path("Plugins/Lights") / plugin));
            lig.emplace_back(light);
        }
    }
    return res;
}

Plugin<Integrator> loadIntegrator(JsonHelper config, PluginHelper helper,
    const fs::path &ptx, optix::Program &prog) {
    std::string plugin = config->toString("Plugin");
    Plugin<Integrator> inte(plugin);
    prog = inte->init(helper, config, fs::path("Plugins/Integrators") / plugin, ptx);
    return inte;
}

Plugin<Driver> loadDriver(JsonHelper config, PluginHelper helper,
    uint2 &filmSize) {
    std::string plugin = config->toString("Plugin");
    Plugin<Driver> driver(plugin);
    filmSize = driver->init(helper, config, fs::path("Plugins/Drivers") / plugin);
    return driver;
}

using GeometryLib = std::map<std::string, Plugin<Geometry>>;

GeometryLib loadGeometries(JsonHelper config, PluginHelper helper) {
    GeometryLib res;
    std::vector<JsonHelper> geos = config->expand();
    for (auto cfg : geos) {
        std::string name = cfg->toString("Name");
        if (res.count(name))
            FATAL << "Geometry \"" << name << "\" has been defined.";
        else {
            std::string plugin = cfg->toString("Plugin");
            Plugin<Geometry> geo(plugin);
            geo->init(helper, cfg, fs::path("Plugins/Geometries") / plugin);
            res.emplace(name, geo);
        }
    }
    return res;
}

struct LightInfo final {
    Mat4 trans;
    LightProgram prog;
};

void parseNode(JsonHelper root, const MaterialLib &mat, const LightLib &light
    , const GeometryLib &geo, std::vector<LightInfo> &lightInst,
    std::vector<std::any> &content, optix::Group group, optix::Context context);

void parseChildren(JsonHelper root, const MaterialLib &mat, const LightLib &light
    , const GeometryLib &geo, std::vector<LightInfo> &lightInst,
    std::vector<std::any> &content, optix::Group group, optix::Context context) {
    std::vector<JsonHelper> children = root->expand();
    for (auto child : children)
        parseNode(child, mat, light, geo, lightInst, content, group, context);
}

template<typename T>
T access(const std::map<std::string, T> &lib, const std::string &name) {
    auto iter = lib.find(name);
    if (iter == lib.cend())
        FATAL << "No " << typeid(T).name() << " called \"" << name << "\".";
    return iter->second;
}

void parseNode(JsonHelper root, const MaterialLib &mat, const LightLib &light
    , const GeometryLib &geo, std::vector<LightInfo> &lightInst,
    std::vector<std::any> &content, optix::Group group, optix::Context context) {
    std::string type = root->toString("Type");
    std::string name = root->toString("Name");
    if (type == "Light") {
        LightInfo info;
        info.trans = Mat4::identity();
        info.prog = access(light, name);
        lightInst.emplace_back(info);
    }
    else if (type == "Geometry") {
        optix::GeometryInstance inst = context->createGeometryInstance();
        access(geo, name)->setInstance(inst);
        inst->setMaterialCount(1);
        inst->setMaterial(0, access(mat, root->toString("Material"))->getMaterial());
        inst->validate();
        content.emplace_back(inst);
        optix::GeometryGroup gg = context->createGeometryGroup();
        optix::Acceleration acc = context->createAcceleration("Trbvh");
        gg->setAcceleration(acc);
        gg->addChild(inst);
        gg->setVisibilityMask(255);
        gg->validate();
        context["globalTopNode"]->set(gg);
        content.emplace_back(gg);
        content.emplace_back(acc);
    }
    else FATAL << "Unrecognized type \"" << type << "\".";
}

void renderImpl(JsonHelper config, const fs::path &scenePath) {
    JsonHelper global = config->attribute("Global");
    optix::Context context = createContext(global);
    std::string runtimeLib = global->toString("RuntimeLib");
    PluginHelper helper = buildPluginHelper(context, fs::current_path() / "Libs" /
        runtimeLib, scenePath);
    INFO << "Loading camera";
    CameraAdapter camera(config->attribute("Camera"), helper);
    INFO << "Loading materials";
    MaterialLib materials = loadMaterials(config->attribute("MaterialLib"), helper);
    INFO << "Loading lights";
    std::vector<Plugin<Light>> lig;
    LightLib lights = loadLights(config->attribute("LightLib"), helper, lig);
    INFO<<"Configurating lights";
    std::vector<int> progs;
    for (auto light : lights)
        progs.push_back(light.second.prog);
    for (auto mat : materials) {
        optix::Program prog = mat.second->getMaterial()->getClosestHitProgram(radianceRayType);
        prog->setCallsitePotentialCallees("call_in_sampleOneLight", progs);
    }
    INFO << "Loading integrator";
    optix::Program inteProg;
    Plugin<Integrator> integrator = loadIntegrator(config->attribute("Integrator"),
        helper, camera.getPTX(), inteProg);
    context->setRayGenerationProgram(0, inteProg);
    INFO << "Loading driver";
    uint2 filmSize;
    Plugin<Driver> driver = loadDriver(config->attribute("Driver"), helper,
        filmSize);
    camera.prepare(inteProg, filmSize);
    INFO << "Loading geometries";
    GeometryLib geos = loadGeometries(config->attribute("GeometryLib"), helper);
    INFO << "Loading scene";
    std::vector<LightInfo> lightInst;
    std::vector<std::any> content;
    optix::Group group = context->createGroup();
    optix::Acceleration acc = context->createAcceleration("Trbvh");
    group->setAcceleration(acc);
    group->setVisibilityMask(255);
    group->validate();
    context["globalTopNode"]->set(group);
    parseChildren(config->attribute("Scene"), materials, lights, geos, lightInst,
        content, group, context);
    optix::Buffer lightMat = context->createBuffer(RT_BUFFER_INPUT,
        RT_FORMAT_USER, lightInst.size());
    {
        lightMat->setElementSize(sizeof(Mat4));
        BufferMapGuard guard(lightMat, RT_BUFFER_MAP_WRITE_DISCARD);
        for (size_t i = 0; i < lightInst.size(); ++i)
            guard.as<Mat4>()[i] = lightInst[i].trans;
    }
    context["lightMatrices"]->set(lightMat);
    optix::Buffer lightProg = context->createBuffer(RT_BUFFER_INPUT,
        RT_FORMAT_USER, lightInst.size());
    {
        lightProg->setElementSize(sizeof(LightProgram));
        BufferMapGuard guard(lightProg, RT_BUFFER_MAP_WRITE_DISCARD);
        for (size_t i = 0; i < lightInst.size(); ++i)
            guard.as<LightProgram>()[i] = lightInst[i].prog;
    }
    context["lightPrograms"]->set(lightProg);
    INFO << "Everything is ready.";
    driver->doRender();
}

bool render(const fs::path &in) {
    if (fs::exists(in)) {
        Json scene = loadScene(in);
        try {
            renderImpl(buildJsonHelper(scene), in.parent_path());
            return true;
        }
        catch (const optix::Exception &ex) {
            FATAL << "Optix Exception[" << ex.getErrorCode() << "]" <<
                ex.getErrorString();
        }
        catch (const std::exception &ex) {
            FATAL << "Exception:" << ex.what();
        }
    }
    ERROR << (in.empty() ?
        "Please specify a scene file." :
        "Invalid scene file name.");
    return false;
}

template<typename T>
void callCommand(const std::string &plugin, int argc, char **argv) {
    try {
        Plugin<T> plu(plugin);
        plu->command(argc, argv);
    }
    catch (const std::exception &ex) {
        FATAL << ex.what();
    }
}

int main(int argc, char **argv) {
    std::vector<std::string> input;
    for (int i = 0; i < argc; ++i)
        input.emplace_back(argv[i]);
    std::vector<char *> ref;
    for (int i = 0; i < argc; ++i)
        ref.emplace_back(input[i].data());
    cxxopts::Options opt("Piper", "Piper::Main");
    opt.allow_unrecognised_options().add_options()(
        "i,input", "scene desc path",
        cxxopts::value<fs::path>()->default_value(
        "testScene.json"))
        ("t,tool", "plugin built-in tools(Type::PluginName)", cxxopts::value<std::string>());
    try {
        auto optRes = opt.parse(argc, argv);
        if (optRes.count("tool")) {
            std::string desc = optRes["tool"].as<std::string>();
            std::regex pat("(Camera|Driver|Geometry|Integrator|Light|Material)::(\\w+)");
            std::smatch mat;
            bool res = std::regex_match(desc, mat, pat);
            res &= mat.size() == 3;
            if (res) {
#define TEST(T) if(mat[1]==#T) callCommand<T>(mat[2],static_cast<int>(ref.size()),ref.data());
                TEST(Camera);
                TEST(Driver);
                TEST(Geometry);
                TEST(Integrator);
                TEST(Light);
                TEST(Material);
#undef TEST
            }
            else FATAL << "Unrecognized tool \"" << desc << "\".";
        }
        else {
            auto scene = optRes["input"].as<fs::path>();
            if (!render(scene))
                std::cout << opt.help() << std::endl;
        }
    }
    catch (const std::exception &ex) {
        FATAL << ex.what();
    }
    return 0;
}
