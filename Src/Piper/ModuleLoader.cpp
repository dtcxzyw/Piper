#include "ModuleLoader.hpp"
#pragma warning(push, 0)
#include <Corrade/PluginManager/Manager.h>
namespace PM = Corrade::PluginManager;
#pragma warning(pop)
#include <exception>
#include <sstream>

template<typename T>
Plugin<T>::Plugin(const std::string& name) {
    static PM::Manager<T> manager;
    auto st = manager.load(name);
    if(st & PM::LoadState::Loaded)
        mPtr.reset(
            manager.instantiate(name).release());
    else {
        std::ostringstream str;
        using Dbg = Corrade::Utility::Debug;
        Dbg(&str, Dbg::Flag::DisableColors |
            Dbg::Flag::NoNewlineAtTheEnd)
            << st;
        throw std::runtime_error(
            "Failed to load plugin " + name+":"+str.str());
    }
}

#include "../Cameras/CameraAPI.hpp"
template class Plugin<Camera>;

#include "../Lights/LightAPI.hpp"
template class Plugin<Light>;

#include "../Materials/MaterialAPI.hpp"
template class Plugin<Material>;

#include "../Drivers/DriverAPI.hpp"
template class Plugin<Driver>;

#include "../Integrators/IntegratorAPI.hpp"
template class Plugin<Integrator>;

#include "../Geometries/GeometryAPI.hpp"
template class Plugin<Geometry>;
