#pragma once

#include "JSONPolygonLoader.h"
#include "PolygonOverlay.h"
#include "PolygonLoader.h"
#include <string>
#include <vector>
#include <map>
#include <SDL2/SDL.h>

/**
 * JSON Polygon File Loader
 *
 * Loads polygon data from JSON files.
 *
 * Cell types (strings) are automatically mapped to integer class IDs.
 */
class JSONPolygonLoader: public PolygonLoader {
public:
    /**
     * Load polygons from JSON file
     * @param filepath Path to .json file
     * @param outPolygons Output vector of polygons
     * @param outClassColors Output map of class ID to color
     * @param outClassNames Output map of class ID to class name
     * @return true if successful, false otherwise
     */
    bool Load(const std::string& filepath,
                    std::vector<Polygon>& outPolygons,
                    std::map<int, SDL_Color>& outClassColors,
                    std::map<int, std::string>& outClassNames) override;
};
