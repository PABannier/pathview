#include "PolygonLoader.h"
#include <iostream>
#include <set>
#include <map>

void PolygonLoader::BuildClassMapping(const std::set<std::string>& cellTypes,
                                      std::map<std::string, int>& outMapping) {
    outMapping.clear();
    int classId = 0;

    for (const auto& cellType : cellTypes) {
        outMapping[cellType] = classId++;
    }
}

// Cell type color map - maps cell type names to RGB colors
static const std::map<std::string, SDL_Color> CELL_TYPE_COLORS = {
    {"Background",         {0, 0, 0, 255}},         // Black
    {"Cancer cell",        {230, 0, 0, 255}},       // Bright Red
    {"Lymphocytes",        {0, 150, 0, 255}},       // Medium Green
    {"Fibroblasts",        {0, 0, 230, 255}},       // Bright Blue
    {"Plasmocytes",        {255, 255, 0, 255}},     // Bright Yellow
    {"Macrophages",        {153, 51, 255, 255}},    // Purple
    {"Eosinophils",        {255, 102, 178, 255}},   // Pink
    {"Muscle Cell",        {102, 51, 0, 255}},      // Brown
    {"Neutrophils",        {255, 153, 51, 255}},    // Orange
    {"Endothelial Cell",   {51, 204, 204, 255}},    // Light Cyan
    {"Red blood cell",     {128, 0, 0, 255}},       // Dark Red
    {"Epithelial",         {0, 102, 0, 255}},       // Dark Green
    {"Mitotic Figures",    {102, 255, 102, 255}},   // Light Green
    {"Apoptotic Body",     {102, 204, 255, 255}},   // Light Blue
    {"Minor Stromal Cell", {255, 153, 102, 255}},   // Light Orange
    {"Other",              {255, 255, 255, 255}}    // White
};

// Fallback colors for unmapped cell types
static const SDL_Color FALLBACK_COLORS[] = {
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

static constexpr size_t NUM_FALLBACK_COLORS = sizeof(FALLBACK_COLORS) / sizeof(FALLBACK_COLORS[0]);

void PolygonLoader::GenerateDefaultColors(int numClasses,
                                          std::map<int, SDL_Color>& outColors) {
    // This function is now a fallback - primary coloring uses class names
    outColors.clear();
    for (int i = 0; i < numClasses; ++i) {
        outColors[i] = FALLBACK_COLORS[i % NUM_FALLBACK_COLORS];
    }
}

void PolygonLoader::GenerateColorsFromClassNames(
    const std::map<std::string, int>& classMapping,
    std::map<int, SDL_Color>& outColors) {

    outColors.clear();
    int fallbackIndex = 0;

    for (const auto& pair : classMapping) {
        const std::string& className = pair.first;
        int classId = pair.second;

        // Look up color in the cell type color map
        auto colorIt = CELL_TYPE_COLORS.find(className);
        if (colorIt != CELL_TYPE_COLORS.end()) {
            outColors[classId] = colorIt->second;
            std::cout << "  Color for '" << className << "' (ID " << classId << "): "
                      << "RGB(" << (int)colorIt->second.r << ", "
                      << (int)colorIt->second.g << ", "
                      << (int)colorIt->second.b << ")" << std::endl;
        } else {
            // Use fallback color for unknown cell types
            outColors[classId] = FALLBACK_COLORS[fallbackIndex % NUM_FALLBACK_COLORS];
            std::cout << "  Unknown cell type '" << className << "' (ID " << classId
                      << "), using fallback color" << std::endl;
            ++fallbackIndex;
        }
    }
}
