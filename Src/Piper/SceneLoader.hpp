#pragma once
#include <filesystem>
#include "../Cameras/CameraAPI.hpp"
#include "../Materials/MaterialAPI.hpp"
#include "../Lights/LightAPI.hpp"
#include "../Integrators/IntegratorAPI.hpp"
#include "../Drivers/DriverAPI.hpp"

namespace fs = std::experimental::filesystem;
Json loadScene(const fs::path& path);
