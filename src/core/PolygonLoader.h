#pragma once

#include "PolygonOverlay.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <SDL2/SDL.h>

/**
 * Base class of Polygon File Loader
 *
 * Defines a common interface for loading polygon data.
 *
 * Cell types (strings) are automatically mapped to integer class IDs.
 */
class PolygonLoader {
public:
    virtual ~PolygonLoader() = default;

    /**
     * Defines a base interface for loading polygon files
     * @param filepath Path to the polygon file
     * @param outPolygons Output vector of polygons
     * @param outClassColors Output map of class ID to color
     * @param outClassNames Output map of class ID to class name
     * @return true if successful, false otherwise
     */
    virtual bool Load(const std::string& filepath,
                     std::vector<Polygon>& outPolygons,
                     std::map<int, SDL_Color>& outClassColors,
                     std::map<int, std::string>& outClassNames) = 0;

protected:
    /**
     * Build class name to ID mapping from unique cell types
     * @param cellTypes Set of unique cell type strings
     * @param outMapping Output map of class name to ID
     */
    static void BuildClassMapping(const std::set<std::string>& cellTypes,
                                  std::map<std::string, int>& outMapping);

    /**
     * Generate default colors for classes (fallback)
     * @param numClasses Number of classes
     * @param outColors Output map of class ID to color
     */
    static void GenerateDefaultColors(int numClasses,
                                      std::map<int, SDL_Color>& outColors);

    /**
     * Generate colors for classes based on cell type names
     * Uses a predefined color map for known cell types.
     * @param classMapping Map of class name to class ID
     * @param outColors Output map of class ID to color
     */
    static void GenerateColorsFromClassNames(
        const std::map<std::string, int>& classMapping,
        std::map<int, SDL_Color>& outColors);
};
