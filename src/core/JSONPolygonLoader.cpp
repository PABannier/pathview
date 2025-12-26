#include "JSONPolygonLoader.h"
#include <fstream>
#include <iostream>

bool JSONPolygonLoader::Load(const std::string& filepath,
                                   std::vector<Polygon>& outPolygons,
                                   std::map<int, SDL_Color>& outClassColors,
                                   std::map<int, std::string>& outClassNames) {
    std::cout << "\n=== Loading JSON Polygon Data ===" << std::endl;
    std::cout << "File: " << filepath << std::endl;

    // Check extension
    if (filepath.find(".json") == std::string::npos) {
        std::cerr << "Invalid file extension: " << filepath << std::endl;
        std::cerr << "Expected .json file" << std::endl;
        return false;
    }

    // TODO
    return true;
}