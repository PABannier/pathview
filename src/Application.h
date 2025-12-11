#pragma once

#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include "Viewport.h"

class SlideLoader;
class SlideRenderer;
class Minimap;
class Viewport;
class TextureManager;
class PolygonOverlay;

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

class Application {
public:
    Application();
    ~Application();

    // Delete copy constructor and assignment
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize();
    void Run();
    void Shutdown();

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

    void ProcessEvents();
    void Update();
    void Render();
    void RenderUI();
    void RenderSlidePreview();

    void OpenFileDialog();
    void LoadSlide(const std::string& path);
    void OpenPolygonFileDialog();
    void LoadPolygons(const std::string& path);

    // UI rendering methods
    void RenderMenuBar();
    void RenderToolbar();
    void RenderSidebar();
    void RenderSlideInfoTab();
    void RenderPolygonTab();
    void RenderAnnotationsTab();

    // Annotation drawing methods
    void HandleAnnotationClick(int x, int y, bool isDoubleClick);
    void CompletePolygon();
    bool IsNearFirstVertex(Vec2 screenPos) const;

    // Annotation rendering methods
    void RenderAnnotations();
    void RenderDrawingPreview();
    void RenderAnnotationPolygon(const AnnotationPolygon& annotation);

    // Annotation management methods
    void DeleteAnnotation(int index);
    void StartRenaming(int index);
    void ComputeCellCounts(AnnotationPolygon& annotation);

    // SDL objects
    SDL_Window* window_;
    SDL_Renderer* renderer_;

    // Application state
    bool running_;
    bool isPanning_;
    int lastMouseX_;
    int lastMouseY_;
    int windowWidth_;
    int windowHeight_;

    // Components
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<SlideLoader> slideLoader_;
    std::unique_ptr<Viewport> viewport_;
    std::unique_ptr<SlideRenderer> slideRenderer_;
    std::unique_ptr<Minimap> minimap_;
    std::unique_ptr<PolygonOverlay> polygonOverlay_;

    // Preview texture (Phase 2 simple display)
    SDL_Texture* previewTexture_;

    // Current slide path
    std::string currentSlidePath_;

    // Sidebar configuration
    static constexpr float SIDEBAR_WIDTH = 350.0f;
    bool sidebarVisible_;

    // ========== ANNOTATION TOOL ==========
    // Annotation tool state
    DrawingState drawingState_;
    std::vector<AnnotationPolygon> annotations_;
    int nextAnnotationId_;
    bool annotationToolActive_;

    // UI state for renaming
    char renameBuffer_[256];
    bool showRenameDialog_;
    int renamingAnnotationIndex_;

    // Toolbar configuration
    static constexpr float TOOLBAR_HEIGHT = 40.0f;

    // Rendering constants
    static constexpr SDL_Color ANNOTATION_COLOR = {255, 255, 0, 255};  // Yellow
    static constexpr float ANNOTATION_OPACITY = 0.3f;
    static constexpr SDL_Color ANNOTATION_OUTLINE_COLOR = {255, 200, 0, 255};
    static constexpr SDL_Color DRAWING_VERTEX_COLOR = {0, 255, 0, 255};  // Green
    static constexpr SDL_Color DRAWING_EDGE_COLOR = {0, 200, 0, 255};
};
