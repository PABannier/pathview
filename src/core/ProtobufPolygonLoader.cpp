#include "ProtobufPolygonLoader.h"
#include "cell_polygons.pb.h"
#include <fstream>
#include <iostream>

bool ProtobufPolygonLoader::Load(const std::string& filepath,
                                 std::vector<Polygon>& outPolygons,
                                 std::map<int, SDL_Color>& outClassColors,
                                 std::map<int, std::string>& outClassNames) {
    std::cout << "\n=== Loading Protobuf Polygon Data ===" << std::endl;
    std::cout << "File: " << filepath << std::endl;

    // Check extension
    if (filepath.find(".pb") == std::string::npos && filepath.find(".protobuf") == std::string::npos) {
        std::cerr << "Invalid file extension: " << filepath << std::endl;
        std::cerr << "Expected .pb or .protobuf file" << std::endl;
        return false;
    }

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
    outClassNames.clear();

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
