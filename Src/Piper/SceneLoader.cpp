#include "SceneLoader.hpp"
#include <fstream>
#include <iostream>
#pragma warning(push, 0)
#include <nlohmann/json.hpp>
#pragma warning(pop)
#include "../Shared.hpp"

using Json = nlohmann::json;
Json loadScene(const fs::path& path) {
    std::ifstream in(path);
    Json sceneData;
    try {
        in >> sceneData;
        return Json;
    } catch(const std::exception& ex) {
        ERROR << ex.what();
        return;
    }
}

