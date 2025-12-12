#include "PolygonLoader.h"
#include "cell_polygons.pb.h"
#include <fstream>
#include <iostream>
#include <set>

bool PolygonLoader::Load(const std::string& filepath,
                         std::vector<Polygon>& outPolygons,
                         std::map<int, SDL_Color>& outClassColors) {
    std::cout << "\n=== Loading Protobuf Polygon Data ===" << std::endl;
    std::cout << "File: " << filepath << std::endl;

    // Read file into string
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open protobuf file: " << filepath << std::endl;
        return false;
    }

    // Parse protobuf message
    histowmics::SlideSegmentationData slideData;
    if (!slideData.ParseFromIstream(&file)) {
        std::cerr << "Failed to parse protobuf message" << std::endl;
        return false;
    }

    std::cout << "Slide ID: " << slideData.slide_id() << std::endl;
    std::cout << "Tiles: " << slideData.tiles_size() << std::endl;

    // Clear output containers
    outPolygons.clear();
    outClassColors.clear();

    int maxDeepZoomLevel = static_cast<int>(slideData.max_level());

    // First pass: collect unique cell types
    std::set<std::string> uniqueCellTypes;
    int totalMasks = 0;

    for (int i = 0; i < slideData.tiles_size(); ++i) {
        const auto& tile = slideData.tiles(i);
        totalMasks += tile.masks_size();

        for (int j = 0; j < tile.masks_size(); ++j) {
            const auto& mask = tile.masks(j);
            uniqueCellTypes.insert(mask.cell_type());
        }
    }

    std::cout << "Total polygons: " << totalMasks << std::endl;
    std::cout << "Unique cell types: " << uniqueCellTypes.size() << std::endl;

    // Build class name to ID mapping
    std::map<std::string, int> classMapping;
    BuildClassMapping(uniqueCellTypes, classMapping);

    // Print mapping
    for (const auto& pair : classMapping) {
        std::cout << "  " << pair.first << " -> Class " << pair.second << std::endl;
    }

    // Generate default colors
    GenerateDefaultColors(static_cast<int>(uniqueCellTypes.size()), outClassColors);

    // Second pass: extract polygons from all tiles
    outPolygons.reserve(totalMasks);

    for (int i = 0; i < slideData.tiles_size(); ++i) {
        const auto& tile = slideData.tiles(i);

        for (int j = 0; j < tile.masks_size(); ++j) {
            const auto& mask = tile.masks(j);

            // Skip if no coordinates
            if (mask.coordinates_size() < 3) {
                continue;
            }

            double scaleFactor = std::pow(2, maxDeepZoomLevel - tile.level());

            Polygon polygon;
            polygon.classId = classMapping[mask.cell_type()];

            // Convert Point (float) to Vec2 (double)
            polygon.vertices.reserve(mask.coordinates_size());
            for (int k = 0; k < mask.coordinates_size(); ++k) {
                const auto& point = mask.coordinates(k);
                polygon.vertices.emplace_back(
                    static_cast<double>((point.x() + tile.x() * tile.width()) * scaleFactor),
                    static_cast<double>((point.y() + tile.y() * tile.height()) * scaleFactor)
                );
            }

            // Compute bounding box
            polygon.ComputeBoundingBox();

            outPolygons.push_back(std::move(polygon));
        }

        // Progress update every 10 tiles
        if ((i + 1) % 10 == 0) {
            std::cout << "  Processed " << (i + 1) << " / " << slideData.tiles_size()
                      << " tiles..." << std::endl;
        }
    }

    std::cout << "Successfully loaded " << outPolygons.size() << " polygons" << std::endl;
    std::cout << "==================================\n" << std::endl;

    return true;
}

void PolygonLoader::BuildClassMapping(const std::set<std::string>& cellTypes,
                                      std::map<std::string, int>& outMapping) {
    outMapping.clear();
    int classId = 0;

    for (const auto& cellType : cellTypes) {
        outMapping[cellType] = classId++;
    }
}

void PolygonLoader::GenerateDefaultColors(int numClasses,
                                          std::map<int, SDL_Color>& outColors) {
    // Default color palette
    static const SDL_Color DEFAULT_COLORS[] = {
        {255, 0, 0, 255},      // Red
        {0, 255, 0, 255},      // Green
        {0, 0, 255, 255},      // Blue
        {255, 255, 0, 255},    // Yellow
        {255, 0, 255, 255},    // Magenta
        {0, 255, 255, 255},    // Cyan
        {255, 128, 0, 255},    // Orange
        {128, 0, 255, 255},    // Purple
        {255, 192, 203, 255},  // Pink
        {128, 128, 128, 255}   // Gray
    };

    static constexpr size_t NUM_DEFAULT_COLORS = sizeof(DEFAULT_COLORS) / sizeof(DEFAULT_COLORS[0]);

    outColors.clear();
    for (int i = 0; i < numClasses; ++i) {
        outColors[i] = DEFAULT_COLORS[i % NUM_DEFAULT_COLORS];
    }
}
