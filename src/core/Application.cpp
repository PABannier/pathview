#include "Application.h"
#include "SlideLoader.h"
#include "TextureManager.h"
#include "Viewport.h"
#include "SlideRenderer.h"
#include "Minimap.h"
#include "PolygonOverlay.h"
#include "AnnotationManager.h"
#include "UIStyle.h"
#include "../api/ipc/IPCServer.h"
#include "../api/ipc/IPCMessage.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "IconsFontAwesome6.h"
#include <nfd.hpp>
#include <iostream>

Application::Application()
    : window_(nullptr)
    , renderer_(nullptr)
    , running_(false)
    , isPanning_(false)
    , lastMouseX_(0)
    , lastMouseY_(0)
    , windowWidth_(1280)
    , windowHeight_(720)
    , dpiScale_(1.0f)
    , previewTexture_(nullptr)
    , sidebarVisible_(true)
{
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

    window_ = SDL_CreateWindow(
        "PathView - Digital Pathology Viewer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowWidth_,
        windowHeight_,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
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

    // Calculate DPI scale factor for high-DPI displays (Retina, etc.)
    // Compare the actual drawable size (in pixels) to the logical window size
    int drawableWidth, drawableHeight;
    SDL_GetRendererOutputSize(renderer_, &drawableWidth, &drawableHeight);
    dpiScale_ = static_cast<float>(drawableWidth) / static_cast<float>(windowWidth_);
    std::cout << "DPI Scale: " << dpiScale_ << " (drawable: " << drawableWidth << "x" << drawableHeight
              << ", window: " << windowWidth_ << "x" << windowHeight_ << ")" << std::endl;

    // Set render scale for high-DPI support
    // This makes SDL scale all rendering operations from logical to native resolution
    // Combined with fonts loaded at native resolution, this gives crisp text
    SDL_RenderSetScale(renderer_, dpiScale_, dpiScale_);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    std::string fontPath = std::string(RESOURCES_DIR) + "/fonts/";
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;

    // Base font sizes (will be scaled by DPI)
    const float baseFontSize = 15.0f;
    const float iconFontSize = 13.0f;
    const float mediumFontSize = 16.0f;

    // Load Inter Regular with DPI scaling
    fontRegular_ = io.Fonts->AddFontFromFileTTF(
        (fontPath + "Inter-Regular.ttf").c_str(), baseFontSize * dpiScale_, &config);

    // Merge Font Awesome icons into the same font
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = iconFontSize * dpiScale_;
    io.Fonts->AddFontFromFileTTF(
        (fontPath + "FontAwesome6-Solid.ttf").c_str(), iconFontSize * dpiScale_,
        &icons_config, icons_ranges);

    fontMedium_ = io.Fonts->AddFontFromFileTTF(
        (fontPath + "Inter-Medium.ttf").c_str(), mediumFontSize * dpiScale_, &config);

    // Set font global scale to compensate (ImGui works in logical coordinates)
    // This makes fonts render at native resolution but display at correct logical size
    io.FontGlobalScale = 1.0f / dpiScale_;

    UIStyle::ApplyStyle();

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

    // Create annotation manager
    annotationManager_ = std::make_unique<AnnotationManager>(renderer_);

    // Create IPC server for remote control
    ipcServer_ = std::make_unique<pathview::ipc::IPCServer>(
        [this](const std::string& method, const pathview::ipc::json& params) {
            return HandleIPCCommand(method, params);
        }
    );

    if (!ipcServer_->Start()) {
        std::cerr << "Warning: Failed to start IPC server (non-fatal)" << std::endl;
        // Non-fatal - GUI works without IPC
    }

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
    if (!window_ && !renderer_) {
        return;
    }

    // Stop IPC server first
    ipcServer_.reset();

    annotationManager_.reset();
    polygonOverlay_.reset();
    minimap_.reset();
    slideRenderer_.reset();
    textureManager_.reset();
    viewport_.reset();
    slideLoader_.reset();

    if (previewTexture_) {
        SDL_DestroyTexture(previewTexture_);
        previewTexture_ = nullptr;
    }

    // Cleanup ImGui (must happen while renderer is still valid)
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    // Cleanup SDL last
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
            if (annotationManager_) {
                annotationManager_->HandleKeyPress(event.key.keysym.sym, polygonOverlay_.get());
            }

            // Handle keyboard shortcuts
            if (event.key.keysym.sym == SDLK_r && viewport_) {
                viewport_->ResetView();
            }
        }

        // Only handle mouse/keyboard if ImGui doesn't want capture
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantCaptureMouse) {
            // Handle annotation tool mouse clicks
            if (annotationManager_ && annotationManager_->IsToolActive() &&
                event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT && viewport_) {
                    annotationManager_->HandleClick(event.button.x, event.button.y,
                                                   event.button.clicks == 2,
                                                   *viewport_, minimap_.get(),
                                                   polygonOverlay_.get());
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
        if (event.type == SDL_MOUSEMOTION && viewport_ && annotationManager_) {
            int mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            annotationManager_->UpdateMousePosition(viewport_->ScreenToSlide(Vec2(mouseX, mouseY)));
        }
    }

    // Process IPC messages (non-blocking, max 10ms per frame for 60 FPS)
    if (ipcServer_) {
        ipcServer_->ProcessMessages(10);
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
    if (annotationManager_ && viewport_) {
        annotationManager_->RenderAnnotations(*viewport_);

        // Render in-progress polygon drawing
        if (annotationManager_->IsDrawing()) {
            annotationManager_->RenderDrawingPreview(*viewport_);
        }
    }

    // Render minimap overlay
    if (slideLoader_ && viewport_ && minimap_) {
        minimap_->Render(*viewport_, false, 0);
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
        bool wasActive = annotationManager_ && annotationManager_->IsToolActive();

        if (wasActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        }

        if (ImGui::Button("Polygon Tool", ImVec2(120, 30))) {
            if (annotationManager_) {
                annotationManager_->SetToolActive(!wasActive);
            }
        }

        if (wasActive) {
            ImGui::PopStyleColor();
        }

        if (annotationManager_ && annotationManager_->IsToolActive()) {
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
                if (annotationManager_) {
                    annotationManager_->RenderUI(polygonOverlay_.get());
                }
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

// IPC command handler
pathview::ipc::json Application::HandleIPCCommand(const std::string& method, const pathview::ipc::json& params) {
    using namespace pathview::ipc;

    try {
        // Viewport commands
        if (method == "viewport.pan") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded");
            }

            double dx = params.at("dx").get<double>();
            double dy = params.at("dy").get<double>();
            viewport_->Pan(Vec2(dx, dy));

            Vec2 pos = viewport_->GetPosition();
            return json{
                {"position", {{"x", pos.x}, {"y", pos.y}}},
                {"zoom", viewport_->GetZoom()}
            };
        }
        else if (method == "viewport.zoom") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded");
            }

            double delta = params.at("delta").get<double>();

            // Zoom at center of viewport
            Vec2 center(windowWidth_ / 2.0, windowHeight_ / 2.0);
            viewport_->ZoomAtPoint(center, delta);

            return json{
                {"zoom", viewport_->GetZoom()},
                {"position", {
                    {"x", viewport_->GetPosition().x},
                    {"y", viewport_->GetPosition().y}
                }}
            };
        }
        else if (method == "viewport.zoom_at_point") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded");
            }

            int screenX = params.at("screen_x").get<int>();
            int screenY = params.at("screen_y").get<int>();
            double delta = params.at("delta").get<double>();

            viewport_->ZoomAtPoint(Vec2(screenX, screenY), delta);

            return json{
                {"zoom", viewport_->GetZoom()},
                {"position", {
                    {"x", viewport_->GetPosition().x},
                    {"y", viewport_->GetPosition().y}
                }}
            };
        }
        else if (method == "viewport.center_on") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded");
            }

            double x = params.at("x").get<double>();
            double y = params.at("y").get<double>();
            viewport_->CenterOn(Vec2(x, y));

            return json{
                {"position", {
                    {"x", viewport_->GetPosition().x},
                    {"y", viewport_->GetPosition().y}
                }},
                {"zoom", viewport_->GetZoom()}
            };
        }
        else if (method == "viewport.reset") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded");
            }

            viewport_->ResetView();

            return json{
                {"position", {
                    {"x", viewport_->GetPosition().x},
                    {"y", viewport_->GetPosition().y}
                }},
                {"zoom", viewport_->GetZoom()}
            };
        }

        // Slide commands
        else if (method == "slide.load") {
            std::string path = params.at("path").get<std::string>();
            LoadSlide(path);

            if (!slideLoader_) {
                throw std::runtime_error("Failed to load slide");
            }

            return json{
                {"width", slideLoader_->GetWidth()},
                {"height", slideLoader_->GetHeight()},
                {"levels", slideLoader_->GetLevelCount()},
                {"path", currentSlidePath_}
            };
        }
        else if (method == "slide.info") {
            if (!slideLoader_) {
                throw std::runtime_error("No slide loaded");
            }

            json result = {
                {"width", slideLoader_->GetWidth()},
                {"height", slideLoader_->GetHeight()},
                {"levels", slideLoader_->GetLevelCount()},
                {"path", currentSlidePath_}
            };

            if (viewport_) {
                result["viewport"] = {
                    {"position", {
                        {"x", viewport_->GetPosition().x},
                        {"y", viewport_->GetPosition().y}
                    }},
                    {"zoom", viewport_->GetZoom()},
                    {"window_width", windowWidth_},
                    {"window_height", windowHeight_}
                };
            }

            return result;
        }

        // Polygon commands
        else if (method == "polygons.load") {
            std::string path = params.at("path").get<std::string>();
            LoadPolygons(path);

            if (!polygonOverlay_) {
                throw std::runtime_error("Failed to load polygons");
            }

            return json{
                {"count", polygonOverlay_->GetPolygonCount()},
                {"classes", polygonOverlay_->GetClassIds()}
            };
        }
        else if (method == "polygons.set_visibility") {
            if (!polygonOverlay_) {
                throw std::runtime_error("No polygons loaded");
            }

            bool visible = params.at("visible").get<bool>();
            polygonOverlay_->SetVisible(visible);

            return json{{"visible", polygonOverlay_->IsVisible()}};
        }
        else if (method == "polygons.query") {
            if (!polygonOverlay_) {
                throw std::runtime_error("No polygons loaded");
            }

            double x = params.at("x").get<double>();
            double y = params.at("y").get<double>();
            double w = params.at("w").get<double>();
            double h = params.at("h").get<double>();

            // TODO: Implement polygon query in PolygonOverlay
            // For now, return empty list
            return json{{"polygons", json::array()}};
        }

        // Unknown method
        throw std::runtime_error("Unknown method: " + method);

    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("JSON error: ") + e.what());
    }
}

