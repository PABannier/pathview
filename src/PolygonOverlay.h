#pragma once

#include "Viewport.h"  // For Vec2 and Rect
#include <SDL2/SDL.h>
#include <vector>
#include <map>
#include <memory>
#include <string>

// Forward declarations
class PolygonIndex;

// Phase 2: Level-of-detail (LOD) enum
enum class LODLevel {
    SKIP,       // < 2 pixels - don't render
    POINT,      // 2-4 pixels - single pixel
    BOX,        // 4-10 pixels - bounding box rectangle
    SIMPLIFIED, // 10-30 pixels - reduced vertices (future enhancement)
    FULL        // 30+ pixels - full detail
};

// Polygon structure
struct Polygon {
    std::vector<Vec2> vertices;  // Slide coordinates (level 0)
    int classId;                 // Cell class identifier

    // Cached for performance
    Rect boundingBox;                          // For quick culling
    mutable std::vector<int> triangleIndices;  // Lazy triangulation

    Polygon() : classId(0) {}

    // Calculate bounding box from vertices
    void ComputeBoundingBox() {
        if (vertices.empty()) {
            boundingBox = Rect(0, 0, 0, 0);
            return;
        }

        double minX = vertices[0].x, maxX = vertices[0].x;
        double minY = vertices[0].y, maxY = vertices[0].y;

        for (const auto& v : vertices) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.y > maxY) maxY = v.y;
        }

        boundingBox = Rect(minX, minY, maxX - minX, maxY - minY);
    }
};

// Main polygon overlay class
class PolygonOverlay {
public:
    explicit PolygonOverlay(SDL_Renderer* renderer);
    ~PolygonOverlay();

    // Delete copy constructor and assignment
    PolygonOverlay(const PolygonOverlay&) = delete;
    PolygonOverlay& operator=(const PolygonOverlay&) = delete;

    // Load polygons from binary file
    bool LoadPolygons(const std::string& filepath);

    // Render polygons for current viewport
    void Render(const Viewport& viewport);

    // Visibility control
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

    // Opacity control (0.0 - 1.0)
    void SetOpacity(float opacity);
    float GetOpacity() const { return opacity_; }

    // Color management
    void SetClassColor(int classId, SDL_Color color);
    SDL_Color GetClassColor(int classId) const;

    // Get class information for UI legend
    const std::vector<int>& GetClassIds() const { return classIds_; }
    int GetPolygonCount() const { return static_cast<int>(polygons_.size()); }
    const std::vector<Polygon>& GetPolygons() const { return polygons_; }

    // Get slide dimensions (for spatial index)
    void SetSlideDimensions(double width, double height) {
        slideWidth_ = width;
        slideHeight_ = height;
    }

private:
    SDL_Renderer* renderer_;
    std::vector<Polygon> polygons_;
    std::unique_ptr<PolygonIndex> spatialIndex_;
    std::map<int, SDL_Color> classColors_;
    std::vector<int> classIds_;  // Ordered list of class IDs
    bool visible_;
    float opacity_;
    double slideWidth_;
    double slideHeight_;

    // Phase 1: LOD configuration
    double minScreenSizePixels_ = 2.0;  // Skip polygons smaller than this

    // Phase 2: LOD thresholds (configurable)
    double lodPointThreshold_ = 4.0;
    double lodBoxThreshold_ = 10.0;
    double lodSimplifiedThreshold_ = 30.0;

    // Rendering helpers
    void RenderPolygonBatch(const std::vector<Polygon*>& batch,
                           int classId,
                           const Viewport& viewport);

    // Phase 2: LOD-specific rendering methods
    LODLevel DeterminePolygonLOD(const Polygon* polygon, const Viewport& viewport) const;
    void RenderAsPoints(const std::vector<Polygon*>& polygons,
                        SDL_Color color, uint8_t alpha, const Viewport& viewport);
    void RenderAsBoxes(const std::vector<Polygon*>& polygons,
                       SDL_Color color, uint8_t alpha, const Viewport& viewport);
    void RenderFull(const std::vector<Polygon*>& polygons,
                    SDL_Color color, uint8_t alpha, const Viewport& viewport);

    // Initialize default colors
    void InitializeDefaultColors();
};
