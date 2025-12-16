#pragma once

#include "Viewport.h"
#include <SDL2/SDL.h>
#include <vector>
#include <map>
#include <string>

// Forward declarations
class PolygonOverlay;
class Minimap;

// Annotation polygon structure
struct AnnotationPolygon {
    std::vector<Vec2> vertices;         // Level 0 slide coordinates
    int id;                             // Unique identifier
    std::string name;                   // User-editable name
    Rect boundingBox;                   // Cached for rendering
    mutable std::vector<int> triangleIndices;  // Lazy triangulation
    std::map<int, int> cellCounts;      // classId -> count of cells inside

    AnnotationPolygon(int id_) : id(id_), name("Polygon " + std::to_string(id_)) {}
    void ComputeBoundingBox();
    bool ContainsPoint(const Vec2& point) const;
};

/**
 * Manages user-created polygon annotations on slides
 * Handles drawing interaction, rendering, and cell counting
 */
class AnnotationManager {
public:
    explicit AnnotationManager(SDL_Renderer* renderer);
    ~AnnotationManager();

    // Delete copy constructor and assignment
    AnnotationManager(const AnnotationManager&) = delete;
    AnnotationManager& operator=(const AnnotationManager&) = delete;

    // Drawing interaction
    void HandleClick(int x, int y, bool isDoubleClick, const Viewport& viewport,
                     const Minimap* minimap, PolygonOverlay* polygonOverlay);
    void HandleKeyPress(SDL_Keycode key, PolygonOverlay* polygonOverlay);
    void UpdateMousePosition(const Vec2& slidePos);

    // Tool state management
    void SetToolActive(bool active);
    bool IsToolActive() const { return toolActive_; }
    bool IsDrawing() const { return drawingState_.isActive; }

    // Rendering
    void RenderAnnotations(const Viewport& viewport);
    void RenderDrawingPreview(const Viewport& viewport);
    void RenderUI(PolygonOverlay* polygonOverlay);

    // Annotation management
    void DeleteAnnotation(int index);
    void StartRenaming(int index);
    const std::vector<AnnotationPolygon>& GetAnnotations() const { return annotations_; }
    int GetAnnotationCount() const { return static_cast<int>(annotations_.size()); }

    // Cell counting
    void ComputeCellCounts(AnnotationPolygon& annotation, PolygonOverlay* polygonOverlay);

    // Programmatic annotation creation (for IPC/MCP)
    int CreateAnnotation(const std::vector<Vec2>& vertices,
                         const std::string& name = "",
                         PolygonOverlay* polygonOverlay = nullptr);

    // Query by ID
    AnnotationPolygon* GetAnnotationById(int id);
    const AnnotationPolygon* GetAnnotationById(int id) const;

    // Delete by ID (returns true if found and deleted)
    bool DeleteAnnotationById(int id);

    // Metrics computation structure
    struct AnnotationMetrics {
        Rect boundingBox;
        double area;
        double perimeter;
        std::map<int, int> cellCounts;
        int totalCells;
    };

    // Compute metrics for arbitrary vertices (no persistence - "quick probe")
    AnnotationMetrics ComputeMetricsForVertices(
        const std::vector<Vec2>& vertices,
        PolygonOverlay* polygonOverlay = nullptr) const;

    // Geometry calculation helpers (public static for testing)
    static double ComputeArea(const std::vector<Vec2>& vertices);
    static double ComputePerimeter(const std::vector<Vec2>& vertices);
    static bool ValidateVertices(const std::vector<Vec2>& vertices);

private:
    // Drawing state structure
    struct DrawingState {
        bool isActive;                      // Currently drawing
        std::vector<Vec2> currentVertices;  // Vertices in slide coords
        Vec2 mouseSlidePos;                 // Current mouse position

        DrawingState() : isActive(false) {}
        void Clear() {
            isActive = false;
            currentVertices.clear();
        }
    };

    SDL_Renderer* renderer_;
    DrawingState drawingState_;
    std::vector<AnnotationPolygon> annotations_;
    int nextAnnotationId_;
    bool toolActive_;

    // UI state for renaming
    char renameBuffer_[256];
    bool showRenameDialog_;
    int renamingAnnotationIndex_;

    // Helper methods
    void CompletePolygon(PolygonOverlay* polygonOverlay);
    bool IsNearFirstVertex(Vec2 screenPos, const Viewport& viewport) const;
    void RenderAnnotationPolygon(const AnnotationPolygon& annotation, const Viewport& viewport);

    // Rendering constants
    static constexpr SDL_Color ANNOTATION_COLOR = {255, 255, 0, 255};  // Yellow
    static constexpr float ANNOTATION_OPACITY = 0.3f;
    static constexpr SDL_Color ANNOTATION_OUTLINE_COLOR = {255, 200, 0, 255};
    static constexpr SDL_Color DRAWING_VERTEX_COLOR = {0, 255, 0, 255};  // Green
    static constexpr SDL_Color DRAWING_EDGE_COLOR = {0, 200, 0, 255};
};
