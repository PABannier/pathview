#include "JSONPolygonLoader.h"
#include "simdjson.h"
#include <fstream>
#include <iostream>
#include <set>
#include <cmath>

bool JSONPolygonLoader::Load(const std::string& filepath,
                                   std::vector<Polygon>& outPolygons,
                                   std::map<int, SDL_Color>& outClassColors,
                                   std::map<int, std::string>& outClassNames) {
    // Read file into string
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open JSON file: " << filepath << std::endl;
        return false;
    }

    // Read entire file into string
    std::string jsonContent((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    file.close();

    // Parse JSON with simdjson
    simdjson::dom::parser parser;
    simdjson::dom::element doc;

    auto error = parser.parse(jsonContent).get(doc);
    if (error) {
        std::cerr << "Failed to parse JSON: " << simdjson::error_message(error) << std::endl;
        return false;
    }

    // Clear output containers
    outPolygons.clear();
    outClassColors.clear();
    outClassNames.clear();

    // Extract slide metadata
    std::string slideId;
    int maxDeepZoomLevel = 0;

    if (doc["slide_id"].error() == simdjson::SUCCESS) {
        slideId = std::string(doc["slide_id"].get_c_str().value());
        std::cout << "Slide ID: " << slideId << std::endl;
    }

    if (doc["max_level"].error() == simdjson::SUCCESS) {
        maxDeepZoomLevel = static_cast<int>(doc["max_level"].get_int64().value());
    }

    // Check if tiles array exists
    simdjson::dom::array tiles;
    if (doc["tiles"].get(tiles)) {
        std::cerr << "No 'tiles' array found in JSON" << std::endl;
        return false;
    }

    std::cout << "Tiles: " << tiles.size() << std::endl;

    // First pass: collect unique cell types
    std::set<std::string> uniqueCellTypes;
    size_t totalMasks = 0;

    for (auto tile : tiles) {
        simdjson::dom::array masks;
        if (tile["masks"].get(masks) == simdjson::SUCCESS) {
            totalMasks += masks.size();

            for (auto mask : masks) {
                std::string_view cellType;
                if (mask["cell_type"].get(cellType) == simdjson::SUCCESS) {
                    uniqueCellTypes.insert(std::string(cellType));
                }
            }
        }
    }

    std::cout << "Total polygons: " << totalMasks << std::endl;
    std::cout << "Unique cell types: " << uniqueCellTypes.size() << std::endl;

    // Build class name to ID mapping
    std::map<std::string, int> classMapping;
    BuildClassMapping(uniqueCellTypes, classMapping);

    // Build reverse mapping (ID to name) for output
    for (const auto& pair : classMapping) {
        outClassNames[pair.second] = pair.first;
    }

    // Print mapping
    for (const auto& pair : classMapping) {
        std::cout << "  " << pair.first << " -> Class " << pair.second << std::endl;
    }

    // Generate colors based on class names
    std::cout << "Assigning colors to cell types:" << std::endl;
    GenerateColorsFromClassNames(classMapping, outClassColors);

    // Second pass: extract polygons from all tiles
    outPolygons.reserve(totalMasks);

    size_t tileCount = 0;
    for (auto tile : tiles) {
        // Extract tile metadata
        int64_t level = 0;
        double tileX = 0.0, tileY = 0.0;
        int64_t tileWidth = 0, tileHeight = 0;

        // Ignore errors if fields are missing, use defaults
        (void)tile["level"].get(level);
        (void)tile["x"].get(tileX);
        (void)tile["y"].get(tileY);
        (void)tile["width"].get(tileWidth);
        (void)tile["height"].get(tileHeight);

        double scaleFactor = std::pow(2, maxDeepZoomLevel - level);

        // Get masks array
        simdjson::dom::array masks;
        if (tile["masks"].get(masks) != simdjson::SUCCESS) {
            continue;
        }

        for (auto mask : masks) {
            // Get cell type
            std::string_view cellType;
            if (mask["cell_type"].get(cellType) != simdjson::SUCCESS) {
                continue;
            }

            // Get coordinates array
            simdjson::dom::array coordinates;
            if (mask["coordinates"].get(coordinates) != simdjson::SUCCESS) {
                continue;
            }

            // Skip if not enough coordinates
            if (coordinates.size() < 3) {
                continue;
            }

            Polygon polygon;
            polygon.classId = classMapping[std::string(cellType)];

            // Extract vertices
            polygon.vertices.reserve(coordinates.size());
            for (auto point : coordinates) {
                double x = 0.0, y = 0.0;

                // Support both object format {x: val, y: val} and array format [x, y]
                if (point.is_object()) {
                    (void)point["x"].get(x);
                    (void)point["y"].get(y);
                } else if (point.is_array()) {
                    simdjson::dom::array pointArray = point.get_array().value();
                    auto it = pointArray.begin();
                    if (it != pointArray.end()) {
                        (void)(*it).get(x);
                        ++it;
                        if (it != pointArray.end()) {
                            (void)(*it).get(y);
                        }
                    }
                }

                polygon.vertices.emplace_back(
                    (x + tileX * tileWidth) * scaleFactor,
                    (y + tileY * tileHeight) * scaleFactor
                );
            }

            // Compute bounding box
            polygon.ComputeBoundingBox();

            outPolygons.push_back(std::move(polygon));
        }

        // Progress update every 10 tiles
        ++tileCount;
        if (tileCount % 10 == 0) {
            std::cout << "  Processed " << tileCount << " / " << tiles.size()
                      << " tiles..." << std::endl;
        }
    }

    std::cout << "Successfully loaded " << outPolygons.size() << " polygons" << std::endl;
    std::cout << "==================================\n" << std::endl;

    return true;
}
