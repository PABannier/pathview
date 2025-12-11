#include "Application.h"
#include "SlideLoader.h"
#include "TextureManager.h"
#include "Viewport.h"
#include "SlideRenderer.h"
#include "Minimap.h"
#include "PolygonOverlay.h"
#include "PolygonTriangulator.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <nfd.hpp>
#include <iostream>
#include <cstring>
#include <cmath>

Application::Application()
    : window_(nullptr)
    , renderer_(nullptr)
    , running_(false)
    , isPanning_(false)
    , lastMouseX_(0)
    , lastMouseY_(0)
    , windowWidth_(1280)
    , windowHeight_(720)
    , previewTexture_(nullptr)
    , sidebarVisible_(true)
    , nextAnnotationId_(1)
    , annotationToolActive_(false)
    , showRenameDialog_(false)
    , renamingAnnotationIndex_(-1)
{
    renameBuffer_[0] = '\0';
}

Application::~Application() {
    Shutdown();
}

bool Application::Initialize() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create window
    window_ = SDL_CreateWindow(
        "PathView - Digital Pathology Viewer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowWidth_,
        windowHeight_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window_) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create renderer
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Initialize ImGui backends
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_)) {
        std::cerr << "Failed to initialize ImGui SDL2 backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer_)) {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend" << std::endl;
        return false;
    }

    // Create texture manager
    textureManager_ = std::make_unique<TextureManager>(renderer_);

    // Create polygon overlay
    polygonOverlay_ = std::make_unique<PolygonOverlay>(renderer_);

    std::cout << "PathView initialized successfully" << std::endl;
    running_ = true;
    return true;
}

void Application::Run() {
    while (running_) {
        ProcessEvents();
        Update();
        Render();
    }
}

void Application::Shutdown() {
    // Cleanup preview texture
    if (previewTexture_) {
        SDL_DestroyTexture(previewTexture_);
        previewTexture_ = nullptr;
    }

    // Cleanup components
    polygonOverlay_.reset();
    minimap_.reset();
    slideRenderer_.reset();
    viewport_.reset();
    slideLoader_.reset();
    textureManager_.reset();

    // Cleanup ImGui
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // Cleanup SDL
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

void Application::ProcessEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Let ImGui handle events first
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Handle application events
        if (event.type == SDL_QUIT) {
            running_ = false;
        }
        else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                windowWidth_ = event.window.data1;
                windowHeight_ = event.window.data2;
                if (viewport_) {
                    viewport_->SetWindowSize(windowWidth_, windowHeight_);
                }
                if (minimap_) {
                    minimap_->SetWindowSize(windowWidth_, windowHeight_);
                }
            }
        }
        else if (event.type == SDL_KEYDOWN) {
            // Handle annotation tool keyboard shortcuts
            if (event.key.keysym.sym == SDLK_ESCAPE && drawingState_.isActive) {
                // Cancel current polygon drawing
                drawingState_.Clear();
            }
            else if ((event.key.keysym.sym == SDLK_RETURN ||
                      event.key.keysym.sym == SDLK_KP_ENTER) &&
                     drawingState_.isActive) {
                // Complete current polygon
                CompletePolygon();
            }
            // Handle keyboard shortcuts
            else if (event.key.keysym.sym == SDLK_r && viewport_) {
                viewport_->ResetView();
            }
        }

        // Only handle mouse/keyboard if ImGui doesn't want capture
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantCaptureMouse) {
            // Handle annotation tool mouse clicks
            if (annotationToolActive_ && event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    HandleAnnotationClick(event.button.x, event.button.y,
                                        event.button.clicks == 2);
                    continue;  // Don't process panning
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                    // Check if click is on minimap first
                    if (minimap_ && viewport_ && minimap_->Contains(event.button.x, event.button.y)) {
                        minimap_->HandleClick(event.button.x, event.button.y, *viewport_);
                    } else {
                        // Start panning
                        isPanning_ = true;
                        lastMouseX_ = event.button.x;
                        lastMouseY_ = event.button.y;
                    }
                }
            }
            else if (event.type == SDL_MOUSEBUTTONUP) {
                if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                    isPanning_ = false;
                }
            }
            else if (event.type == SDL_MOUSEMOTION && isPanning_ && viewport_) {
                // Pan: calculate delta in screen space, convert to slide space
                int deltaX = event.motion.x - lastMouseX_;
                int deltaY = event.motion.y - lastMouseY_;

                // Convert screen delta to slide delta (negative because we're moving the viewport)
                Vec2 slideDelta(-deltaX / viewport_->GetZoom(), -deltaY / viewport_->GetZoom());
                viewport_->Pan(slideDelta);

                lastMouseX_ = event.motion.x;
                lastMouseY_ = event.motion.y;
            }
            else if (event.type == SDL_MOUSEWHEEL && viewport_) {
                // Zoom at cursor position
                int mouseX, mouseY;
                SDL_GetMouseState(&mouseX, &mouseY);

                double zoomFactor = (event.wheel.y > 0) ? 1.1 : 0.9;
                viewport_->ZoomAtPoint(Vec2(mouseX, mouseY), zoomFactor);
            }
        }

        // Track mouse position for drawing preview
        if (event.type == SDL_MOUSEMOTION && viewport_) {
            int mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            drawingState_.mouseSlidePos = viewport_->ScreenToSlide(Vec2(mouseX, mouseY));
        }
    }
}

void Application::Update() {
    // Update logic will be added in later phases
}

void Application::Render() {
    // Start ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Build UI
    RenderUI();

    // Clear screen
    SDL_SetRenderDrawColor(renderer_, 32, 32, 32, 255);
    SDL_RenderClear(renderer_);

    // Render slide using viewport and renderer
    if (slideLoader_ && viewport_ && slideRenderer_) {
        slideRenderer_->Render(*viewport_);
    }
    // Fallback to preview for slides loaded in Phase 2 without viewport
    else if (slideLoader_ && previewTexture_) {
        RenderSlidePreview();
    }

    // Render polygon overlays
    if (polygonOverlay_ && viewport_ && polygonOverlay_->IsVisible()) {
        polygonOverlay_->Render(*viewport_);
    }

    // Render annotation polygons
    RenderAnnotations();

    // Render in-progress polygon drawing
    if (drawingState_.isActive) {
        RenderDrawingPreview();
    }

    // Render minimap overlay
    if (slideLoader_ && viewport_ && minimap_) {
        minimap_->Render(*viewport_, sidebarVisible_, SIDEBAR_WIDTH);
    }

    // Render ImGui
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);

    // Present
    SDL_RenderPresent(renderer_);
}

void Application::RenderUI() {
    RenderMenuBar();
    RenderToolbar();
    RenderSidebar();
}

void Application::OpenFileDialog() {
    // Initialize NFD
    NFD::Guard nfdGuard;

    // Configure file filters for whole-slide image formats
    nfdfilteritem_t filters[] = {
        { "Whole-Slide Images", "svs,tiff,tif,ndpi,vms,vmu,scn,mrxs,bif,svslide" },
        { "All Files", "*" }
    };

    // Open file dialog
    NFD::UniquePath outPath;
    nfdresult_t result = NFD::OpenDialog(outPath, filters, 2);

    if (result == NFD_OKAY) {
        std::cout << "Selected file: " << outPath.get() << std::endl;
        LoadSlide(outPath.get());
    }
    else if (result == NFD_CANCEL) {
        std::cout << "File dialog cancelled" << std::endl;
    }
    else {
        std::cerr << "File dialog error: " << NFD::GetError() << std::endl;
    }
}

void Application::LoadSlide(const std::string& path) {
    currentSlidePath_ = path;
    std::cout << "\n=== Loading Slide ===" << std::endl;
    std::cout << "Path: " << path << std::endl;

    // Clean up previous preview texture
    if (previewTexture_) {
        SDL_DestroyTexture(previewTexture_);
        previewTexture_ = nullptr;
    }

    // Create new slide loader
    slideLoader_ = std::make_unique<SlideLoader>(path);

    if (!slideLoader_->IsValid()) {
        std::cerr << "Failed to load slide: " << slideLoader_->GetError() << std::endl;
        slideLoader_.reset();
        return;
    }

    std::cout << "Slide loaded successfully!" << std::endl;

    // Create viewport for interactive navigation
    viewport_ = std::make_unique<Viewport>(
        windowWidth_,
        windowHeight_,
        slideLoader_->GetWidth(),
        slideLoader_->GetHeight()
    );

    // Create slide renderer
    slideRenderer_ = std::make_unique<SlideRenderer>(
        slideLoader_.get(),
        renderer_,
        textureManager_.get()
    );

    // Create minimap
    minimap_ = std::make_unique<Minimap>(
        slideLoader_.get(),
        renderer_,
        windowWidth_,
        windowHeight_
    );

    // Set slide dimensions in polygon overlay for spatial indexing
    if (polygonOverlay_) {
        polygonOverlay_->SetSlideDimensions(
            static_cast<double>(slideLoader_->GetWidth()),
            static_cast<double>(slideLoader_->GetHeight())
        );
    }

    std::cout << "Viewport, renderer, and minimap created" << std::endl;
    std::cout << "===================\n" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Mouse wheel: Zoom in/out" << std::endl;
    std::cout << "  - Click + drag: Pan" << std::endl;
    std::cout << "  - Click on minimap: Jump to location" << std::endl;
    std::cout << "  - 'R' or View -> Reset View: Reset to fit" << std::endl;
    std::cout << "===================\n" << std::endl;
}

void Application::OpenPolygonFileDialog() {
    // Initialize NFD
    NFD::Guard nfdGuard;

    // Configure file filters for polygon data files
    nfdfilteritem_t filters[] = {
        { "Polygon Data", "pb,protobuf,bin" },
        { "All Files", "*" }
    };

    // Open file dialog
    NFD::UniquePath outPath;
    nfdresult_t result = NFD::OpenDialog(outPath, filters, 2);

    if (result == NFD_OKAY) {
        std::cout << "Selected polygon file: " << outPath.get() << std::endl;
        LoadPolygons(outPath.get());
    }
    else if (result == NFD_CANCEL) {
        std::cout << "Polygon file dialog cancelled" << std::endl;
    }
    else {
        std::cerr << "Polygon file dialog error: " << NFD::GetError() << std::endl;
    }
}

void Application::LoadPolygons(const std::string& path) {
    if (!polygonOverlay_) {
        std::cerr << "Polygon overlay not initialized" << std::endl;
        return;
    }

    if (polygonOverlay_->LoadPolygons(path)) {
        std::cout << "Polygons loaded successfully from: " << path << std::endl;
        // Automatically enable visibility after loading
        polygonOverlay_->SetVisible(true);
    } else {
        std::cerr << "Failed to load polygons from: " << path << std::endl;
    }
}

void Application::RenderSlidePreview() {
    if (!previewTexture_) {
        return;
    }

    // Get texture dimensions
    int texWidth, texHeight;
    SDL_QueryTexture(previewTexture_, nullptr, nullptr, &texWidth, &texHeight);

    // Calculate destination rectangle to fit the preview in the window
    // while maintaining aspect ratio
    float scaleX = static_cast<float>(windowWidth_) / texWidth;
    float scaleY = static_cast<float>(windowHeight_) / texHeight;
    float scale = std::min(scaleX, scaleY) * 0.9f; // 90% of window size

    int dstWidth = static_cast<int>(texWidth * scale);
    int dstHeight = static_cast<int>(texHeight * scale);
    int dstX = (windowWidth_ - dstWidth) / 2;
    int dstY = (windowHeight_ - dstHeight) / 2;

    SDL_Rect dstRect = {dstX, dstY, dstWidth, dstHeight};

    // Render the texture
    SDL_RenderCopy(renderer_, previewTexture_, nullptr, &dstRect);
}

void Application::RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Slide...", "Ctrl+O")) {
                OpenFileDialog();
            }
            if (ImGui::MenuItem("Load Polygons...", "Ctrl+P")) {
                OpenPolygonFileDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Ctrl+Q")) {
                running_ = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset View", "R") && viewport_) {
                viewport_->ResetView();
            }
            ImGui::MenuItem("Toggle Sidebar", nullptr, &sidebarVisible_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                // Could add an about dialog
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void Application::RenderToolbar() {
    float menuBarHeight = ImGui::GetFrameHeight();

    // Position toolbar below menu bar
    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowWidth_), TOOLBAR_HEIGHT), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar;

    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        // Polygon tool button (toggle style)
        // Save state before button to ensure push/pop balance
        bool wasActive = annotationToolActive_;

        if (wasActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        }

        if (ImGui::Button("Polygon Tool", ImVec2(120, 30))) {
            annotationToolActive_ = !annotationToolActive_;
            if (!annotationToolActive_) {
                drawingState_.Clear();  // Cancel any active drawing
            }
        }

        if (wasActive) {
            ImGui::PopStyleColor();
        }

        if (annotationToolActive_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
                              "Click to add vertices | Enter/Double-click/Click first point to close | Esc to cancel");
        }
    }
    ImGui::End();
}

void Application::RenderSidebar() {
    if (!sidebarVisible_) {
        return;  // Sidebar is hidden
    }

    // Calculate sidebar position and size
    float menuBarHeight = ImGui::GetFrameHeight();
    ImVec2 sidebarPos(windowWidth_ - SIDEBAR_WIDTH, menuBarHeight + TOOLBAR_HEIGHT);
    ImVec2 sidebarSize(SIDEBAR_WIDTH, windowHeight_ - menuBarHeight - TOOLBAR_HEIGHT);

    // Position and size the sidebar
    ImGui::SetNextWindowPos(sidebarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(sidebarSize, ImGuiCond_Always);

    // Create fixed sidebar window without title bar
    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar;

    if (ImGui::Begin("##Sidebar", nullptr, windowFlags)) {
        // Create tab bar
        if (ImGui::BeginTabBar("SidebarTabs", ImGuiTabBarFlags_None)) {

            // Tab 1: Slide Information
            if (ImGui::BeginTabItem("Slide Information")) {
                RenderSlideInfoTab();
                ImGui::EndTabItem();
            }

            // Tab 2: Cell Polygons
            if (ImGui::BeginTabItem("Cell Polygons")) {
                RenderPolygonTab();
                ImGui::EndTabItem();
            }

            // Tab 3: Polygon Annotations
            if (ImGui::BeginTabItem("Polygon Annotations")) {
                RenderAnnotationsTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void Application::RenderSlideInfoTab() {
    if (!slideLoader_ || !slideLoader_->IsValid()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No slide loaded");
        ImGui::Text("Use File -> Open Slide...");
        return;
    }

    ImGui::Text("Slide: %s", currentSlidePath_.c_str());
    ImGui::Separator();
    ImGui::Text("Dimensions: %lld x %lld",
                slideLoader_->GetWidth(),
                slideLoader_->GetHeight());
    ImGui::Text("Levels: %d", slideLoader_->GetLevelCount());

    if (viewport_) {
        ImGui::Separator();
        ImGui::Text("Zoom: %.1f%%", viewport_->GetZoom() * 100.0);
        auto pos = viewport_->GetPosition();
        ImGui::Text("Position: (%.0f, %.0f)", pos.x, pos.y);
        auto visible = viewport_->GetVisibleRegion();
        ImGui::Text("Visible: %.0fx%.0f", visible.width, visible.height);
    }

    if (slideRenderer_) {
        ImGui::Separator();
        ImGui::Text("Tile Cache:");
        ImGui::Text("  Tiles: %zu", slideRenderer_->GetCacheTileCount());
        ImGui::Text("  Memory: %.1f MB",
                    slideRenderer_->GetCacheMemoryUsage() / (1024.0 * 1024.0));
        ImGui::Text("  Hit rate: %.1f%%",
                    slideRenderer_->GetCacheHitRate() * 100.0);
    }

    ImGui::Separator();
    for (int i = 0; i < slideLoader_->GetLevelCount(); ++i) {
        auto dims = slideLoader_->GetLevelDimensions(i);
        double downsample = slideLoader_->GetLevelDownsample(i);
        ImGui::Text("  Level %d: %lld x %lld (%.1fx)",
                    i, dims.width, dims.height, downsample);
    }
}

void Application::RenderPolygonTab() {
    if (!polygonOverlay_) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "Overlay not initialized");
        return;
    }

    // Visibility checkbox
    bool visible = polygonOverlay_->IsVisible();
    if (ImGui::Checkbox("Show Polygons", &visible)) {
        polygonOverlay_->SetVisible(visible);
    }

    // Opacity slider
    float opacity = polygonOverlay_->GetOpacity();
    if (ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f")) {
        polygonOverlay_->SetOpacity(opacity);
    }

    // Color controls (only when polygons loaded)
    if (polygonOverlay_->GetPolygonCount() > 0) {
        ImGui::Separator();
        ImGui::Text("Class Colors:");

        for (int classId : polygonOverlay_->GetClassIds()) {
            SDL_Color color = polygonOverlay_->GetClassColor(classId);
            ImVec4 imColor(color.r / 255.0f,
                          color.g / 255.0f,
                          color.b / 255.0f,
                          1.0f);

            ImGui::PushID(classId);
            if (ImGui::ColorEdit3(("Class " + std::to_string(classId)).c_str(),
                                  (float*)&imColor,
                                  ImGuiColorEditFlags_NoInputs)) {
                polygonOverlay_->SetClassColor(classId, {
                    static_cast<uint8_t>(imColor.x * 255),
                    static_cast<uint8_t>(imColor.y * 255),
                    static_cast<uint8_t>(imColor.z * 255),
                    255
                });
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Text("Polygons: %d", polygonOverlay_->GetPolygonCount());
    } else {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "No polygons loaded");
        ImGui::Text("Use File -> Load Polygons...");
    }
}

// ========== ANNOTATION POLYGON METHODS ==========

void AnnotationPolygon::ComputeBoundingBox() {
    if (vertices.empty()) {
        boundingBox = Rect{0, 0, 0, 0};
        return;
    }

    double minX = vertices[0].x, maxX = vertices[0].x;
    double minY = vertices[0].y, maxY = vertices[0].y;

    for (const auto& v : vertices) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }

    boundingBox = Rect{minX, minY, maxX - minX, maxY - minY};
}

bool AnnotationPolygon::ContainsPoint(const Vec2& point) const {
    if (vertices.size() < 3) return false;

    // Ray casting algorithm: cast a ray from point to infinity
    // and count how many times it crosses the polygon edges
    bool inside = false;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        if (((vertices[i].y > point.y) != (vertices[j].y > point.y)) &&
            (point.x < (vertices[j].x - vertices[i].x) * (point.y - vertices[i].y) /
                       (vertices[j].y - vertices[i].y) + vertices[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

// ========== ANNOTATION DRAWING METHODS ==========

void Application::HandleAnnotationClick(int x, int y, bool isDoubleClick) {
    if (!viewport_) return;

    // Skip if click is on minimap
    if (minimap_ && minimap_->Contains(x, y)) return;

    Vec2 screenPos(x, y);
    Vec2 slidePos = viewport_->ScreenToSlide(screenPos);

    // Double-click completes polygon
    if (isDoubleClick && drawingState_.isActive &&
        drawingState_.currentVertices.size() >= 3) {
        CompletePolygon();
        return;
    }

    // Check if clicking near first vertex (close polygon)
    if (drawingState_.isActive &&
        drawingState_.currentVertices.size() >= 3 &&
        IsNearFirstVertex(screenPos)) {
        CompletePolygon();
        return;
    }

    // Add new vertex
    if (!drawingState_.isActive) {
        drawingState_.isActive = true;
    }
    drawingState_.currentVertices.push_back(slidePos);
}

bool Application::IsNearFirstVertex(Vec2 screenPos) const {
    if (drawingState_.currentVertices.empty() || !viewport_) {
        return false;
    }

    Vec2 firstScreenPos = viewport_->SlideToScreen(drawingState_.currentVertices[0]);
    double distance = std::sqrt(
        std::pow(screenPos.x - firstScreenPos.x, 2) +
        std::pow(screenPos.y - firstScreenPos.y, 2)
    );

    return distance < 10.0;  // 10 pixel threshold
}

void Application::CompletePolygon() {
    if (!drawingState_.isActive || drawingState_.currentVertices.size() < 3) {
        return;
    }

    // Create new annotation
    AnnotationPolygon annotation(nextAnnotationId_++);
    annotation.vertices = drawingState_.currentVertices;
    annotation.ComputeBoundingBox();

    annotations_.push_back(annotation);

    std::cout << "Created annotation: " << annotation.name
              << " with " << annotation.vertices.size() << " vertices" << std::endl;

    // Clear drawing state but keep tool active
    drawingState_.Clear();
    drawingState_.isActive = false;

    // Compute cell counts if polygon overlay is loaded
    ComputeCellCounts(annotations_.back());
}

// ========== ANNOTATION RENDERING METHODS ==========

void Application::RenderAnnotations() {
    if (!viewport_ || annotations_.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const auto& annotation : annotations_) {
        RenderAnnotationPolygon(annotation);
    }
}

void Application::RenderAnnotationPolygon(const AnnotationPolygon& annotation) {
    if (annotation.vertices.size() < 3) return;

    // Render filled polygon
    if (annotation.triangleIndices.empty()) {
        annotation.triangleIndices = PolygonTriangulator::Triangulate(annotation.vertices);
    }

    if (!annotation.triangleIndices.empty()) {
        std::vector<SDL_Vertex> vertices;

        uint8_t alpha = static_cast<uint8_t>(ANNOTATION_OPACITY * 255);

        // Transform vertices to screen space
        for (const auto& vertex : annotation.vertices) {
            Vec2 screenPos = viewport_->SlideToScreen(vertex);
            SDL_Vertex sdlVertex;
            sdlVertex.position = {static_cast<float>(screenPos.x),
                                 static_cast<float>(screenPos.y)};
            sdlVertex.color = {ANNOTATION_COLOR.r, ANNOTATION_COLOR.g,
                              ANNOTATION_COLOR.b, alpha};
            sdlVertex.tex_coord = {0.0f, 0.0f};
            vertices.push_back(sdlVertex);
        }

        // Render filled triangles
        SDL_RenderGeometry(renderer_, nullptr,
            vertices.data(), static_cast<int>(vertices.size()),
            annotation.triangleIndices.data(),
            static_cast<int>(annotation.triangleIndices.size()));
    }

    // Render outline
    SDL_SetRenderDrawColor(renderer_, ANNOTATION_OUTLINE_COLOR.r,
                          ANNOTATION_OUTLINE_COLOR.g,
                          ANNOTATION_OUTLINE_COLOR.b, 255);

    for (size_t i = 0; i < annotation.vertices.size(); ++i) {
        Vec2 v1 = viewport_->SlideToScreen(annotation.vertices[i]);
        Vec2 v2 = viewport_->SlideToScreen(
            annotation.vertices[(i + 1) % annotation.vertices.size()]);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(v1.x), static_cast<int>(v1.y),
            static_cast<int>(v2.x), static_cast<int>(v2.y));
    }
}

void Application::RenderDrawingPreview() {
    if (!viewport_ || drawingState_.currentVertices.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Render vertices as circles
    SDL_SetRenderDrawColor(renderer_, DRAWING_VERTEX_COLOR.r,
                          DRAWING_VERTEX_COLOR.g,
                          DRAWING_VERTEX_COLOR.b, 255);

    for (const auto& vertex : drawingState_.currentVertices) {
        Vec2 screenPos = viewport_->SlideToScreen(vertex);
        int x = static_cast<int>(screenPos.x);
        int y = static_cast<int>(screenPos.y);

        // Draw a small circle (5 pixel radius)
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                if (dx*dx + dy*dy <= 25) {  // Circle equation
                    SDL_RenderDrawPoint(renderer_, x + dx, y + dy);
                }
            }
        }
    }

    // Render edges between vertices
    SDL_SetRenderDrawColor(renderer_, DRAWING_EDGE_COLOR.r,
                          DRAWING_EDGE_COLOR.g,
                          DRAWING_EDGE_COLOR.b, 255);

    for (size_t i = 0; i < drawingState_.currentVertices.size() - 1; ++i) {
        Vec2 v1 = viewport_->SlideToScreen(drawingState_.currentVertices[i]);
        Vec2 v2 = viewport_->SlideToScreen(drawingState_.currentVertices[i + 1]);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(v1.x), static_cast<int>(v1.y),
            static_cast<int>(v2.x), static_cast<int>(v2.y));
    }

    // Render preview edge from last vertex to mouse
    if (!drawingState_.currentVertices.empty()) {
        Vec2 lastVertex = viewport_->SlideToScreen(
            drawingState_.currentVertices.back());
        Vec2 mouseScreen = viewport_->SlideToScreen(drawingState_.mouseSlidePos);

        // Draw dashed line
        SDL_SetRenderDrawColor(renderer_, 150, 150, 150, 200);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(lastVertex.x), static_cast<int>(lastVertex.y),
            static_cast<int>(mouseScreen.x), static_cast<int>(mouseScreen.y));
    }

    // Highlight first vertex if close enough to close
    if (drawingState_.currentVertices.size() >= 3) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        if (IsNearFirstVertex(Vec2(mouseX, mouseY))) {
            Vec2 firstScreen = viewport_->SlideToScreen(
                drawingState_.currentVertices[0]);
            int x = static_cast<int>(firstScreen.x);
            int y = static_cast<int>(firstScreen.y);

            // Draw larger highlight circle
            SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 200);
            for (int dy = -8; dy <= 8; ++dy) {
                for (int dx = -8; dx <= 8; ++dx) {
                    if (dx*dx + dy*dy <= 64) {
                        SDL_RenderDrawPoint(renderer_, x + dx, y + dy);
                    }
                }
            }
        }
    }
}

// ========== ANNOTATION MANAGEMENT METHODS ==========

void Application::RenderAnnotationsTab() {
    ImGui::Text("Annotations: %d", static_cast<int>(annotations_.size()));

    if (annotationToolActive_) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                          "Drawing mode active");
    }

    ImGui::Separator();

    if (annotations_.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "No annotations yet");
        ImGui::Text("Use the Polygon Tool to draw annotations");
        return;
    }

    // List annotations
    ImGui::BeginChild("AnnotationList", ImVec2(0, 0), true);

    int deleteIndex = -1;  // Track which to delete outside the loop

    for (int i = 0; i < static_cast<int>(annotations_.size()); ++i) {
        ImGui::PushID(i);

        auto& annotation = annotations_[i];

        // Annotation name (double-click to rename)
        if (ImGui::Selectable(annotation.name.c_str(), false,
                             ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                StartRenaming(i);
            }
        }

        // Delete button
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            deleteIndex = i;
        }

        // Show vertex count
        ImGui::SameLine();
        ImGui::TextDisabled("(%d vertices)",
                           static_cast<int>(annotation.vertices.size()));

        // Show cell counts if available
        if (!annotation.cellCounts.empty()) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Cells inside:");

            int totalCells = 0;
            for (const auto& [classId, count] : annotation.cellCounts) {
                totalCells += count;
                // Get class color from polygon overlay if available
                SDL_Color classColor = {200, 200, 200, 255};  // Default gray
                if (polygonOverlay_) {
                    classColor = polygonOverlay_->GetClassColor(classId);
                }

                ImGui::BulletText("Class %d: %d", classId, count);
                ImGui::SameLine();
                // Show color indicator
                ImGui::ColorButton("##color", ImVec4(classColor.r / 255.0f,
                                                     classColor.g / 255.0f,
                                                     classColor.b / 255.0f,
                                                     1.0f),
                                  ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                                  ImVec2(12, 12));
            }

            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Total: %d cells", totalCells);
            ImGui::Unindent();
        } else if (polygonOverlay_ && polygonOverlay_->GetPolygonCount() > 0) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No cells inside");
            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    // Delete after the loop to avoid iterator issues
    if (deleteIndex >= 0) {
        DeleteAnnotation(deleteIndex);
    }

    // Rename dialog - open popup when flag is set
    if (showRenameDialog_) {
        ImGui::OpenPopup("Rename Annotation");
        showRenameDialog_ = false;  // Only open once
    }

    // Render rename modal
    if (ImGui::BeginPopupModal("Rename Annotation", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter new name:");

        // Set focus on first frame
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        bool submitRename = ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                                             ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::Button("OK", ImVec2(120, 0)) || submitRename) {
            if (renamingAnnotationIndex_ >= 0 &&
                renamingAnnotationIndex_ < static_cast<int>(annotations_.size())) {
                annotations_[renamingAnnotationIndex_].name = renameBuffer_;
            }
            renamingAnnotationIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            renamingAnnotationIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void Application::DeleteAnnotation(int index) {
    if (index >= 0 && index < static_cast<int>(annotations_.size())) {
        std::cout << "Deleting annotation: " << annotations_[index].name << std::endl;
        annotations_.erase(annotations_.begin() + index);
    }
}

void Application::StartRenaming(int index) {
    if (index >= 0 && index < static_cast<int>(annotations_.size())) {
        renamingAnnotationIndex_ = index;
        strncpy(renameBuffer_, annotations_[index].name.c_str(),
                sizeof(renameBuffer_) - 1);
        renameBuffer_[sizeof(renameBuffer_) - 1] = '\0';
        showRenameDialog_ = true;
    }
}

void Application::ComputeCellCounts(AnnotationPolygon& annotation) {
    annotation.cellCounts.clear();

    if (!polygonOverlay_) return;

    // Get all cell polygons from the overlay
    const auto& cellPolygons = polygonOverlay_->GetPolygons();

    // For each cell polygon, check if its centroid is inside the annotation
    for (const auto& cellPolygon : cellPolygons) {
        if (cellPolygon.vertices.empty()) continue;

        // Compute centroid of cell polygon
        Vec2 centroid(0, 0);
        for (const auto& vertex : cellPolygon.vertices) {
            centroid.x += vertex.x;
            centroid.y += vertex.y;
        }
        centroid.x /= cellPolygon.vertices.size();
        centroid.y /= cellPolygon.vertices.size();

        // Check if centroid is inside annotation polygon
        if (annotation.ContainsPoint(centroid)) {
            annotation.cellCounts[cellPolygon.classId]++;
        }
    }

    std::cout << "Computed cell counts for " << annotation.name << ": ";
    for (const auto& [classId, count] : annotation.cellCounts) {
        std::cout << "Class " << classId << ": " << count << " ";
    }
    std::cout << std::endl;
}
