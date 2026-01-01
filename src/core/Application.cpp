#include "Application.h"
#include "SlideLoader.h"
#include "TextureManager.h"
#include "Viewport.h"
#include "SlideRenderer.h"
#include "Minimap.h"
#include "PolygonOverlay.h"
#include "AnnotationManager.h"
#include "NavigationLock.h"
#include "UIStyle.h"
#include "PNGEncoder.h"
#include "ScreenshotBuffer.h"
#include "../api/ipc/IPCServer.h"
#include "../api/ipc/IPCMessage.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "IconsFontAwesome6.h"
#include <SDL_timer.h>
#include <nfd.hpp>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <cfloat>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>

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
    , navLock_(std::make_unique<NavigationLock>())
    , screenshotBuffer_(std::make_unique<pathview::ScreenshotBuffer>())
{
}

Application::~Application() {
    Shutdown();
}

bool Application::IsNavigationLocked() const {
    return navLock_->IsLocked() && !navLock_->IsExpired();
}

bool Application::IsNavigationOwnedByClient(socket_t clientFd) const {
    if (!navLock_->IsLocked() || navLock_->IsExpired()) {
        return true;  // No lock active, anyone can navigate
    }
    return navLock_->GetClientFd() == clientFd;
}

void Application::CheckLockExpiry() {
    if (navLock_->IsLocked() && navLock_->IsExpired()) {
        std::cout << "Navigation lock expired for owner: "
                  << navLock_->GetOwnerUUID() << std::endl;
        navLock_->Reset();
    }
}

std::string Application::GenerateUUID() const {
    // Cross-platform UUID v4 generation using <random>
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);
    
    // Set UUID version 4 and variant bits
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << (ab >> 32) << "-"
       << std::setw(4) << ((ab >> 16) & 0xFFFF) << "-"
       << std::setw(4) << (ab & 0xFFFF) << "-"
       << std::setw(4) << (cd >> 48) << "-"
       << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);
    return ss.str();
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
    } else {
        // Set disconnect callback for lock auto-release
        ipcServer_->SetDisconnectCallback([this](socket_t clientFd) {
            if (navLock_->IsLocked() && navLock_->GetClientFd() == clientFd) {
                std::cout << "IPC client disconnected, releasing navigation lock for owner: "
                          << navLock_->GetOwnerUUID() << std::endl;
                navLock_->Reset();
            }
        });
    }

    std::cout << "PathView initialized successfully" << std::endl;
    running_ = true;
    return true;
}

void Application::Run() {
    lastFrameTime_ = SDL_GetTicks();

    while (running_) {
        // Calculate delta time
        uint32_t currentTime = SDL_GetTicks();
        deltaTime_ = (currentTime - lastFrameTime_) / 1000.0;
        lastFrameTime_ = currentTime;

        // Cap delta time to prevent huge jumps
        if (deltaTime_ > 0.1) {
            deltaTime_ = 0.1;
        }

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
                    int minimapHeight = std::max(0, windowHeight_ - static_cast<int>(STATUS_BAR_HEIGHT));
                    minimap_->SetWindowSize(windowWidth_, minimapHeight);
                }
            }
        }
        else if (event.type == SDL_KEYDOWN) {
            // Check navigation lock FIRST
            if (IsNavigationLocked()) {
                // Block ALL navigation shortcuts when locked
                // Only allow quit shortcuts (handled separately)
                const SDL_Keymod mods = SDL_GetModState();
                bool shortcutMod = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;

                // Still allow Cmd/Ctrl+Q to quit
                if (shortcutMod && event.key.keysym.sym == SDLK_q) {
                    running_ = false;
                }
                continue;  // Skip all other keyboard input
            }

            // Handle annotation tool keyboard shortcuts
            if (annotationManager_) {
                annotationManager_->HandleKeyPress(event.key.keysym.sym, polygonOverlay_.get());
            }

            // Handle keyboard shortcuts
            if (event.key.keysym.sym == SDLK_r && viewport_) {
                viewport_->ResetView();
            }

            const SDL_Keymod mods = SDL_GetModState();
            bool shortcutMod = (mods & (KMOD_CTRL | KMOD_GUI)) != 0;

            if (shortcutMod && event.key.repeat == 0) {
                switch (event.key.keysym.sym) {
                    case SDLK_o:
                        OpenFileDialog();
                        break;
                    case SDLK_p:
                        OpenPolygonFileDialog();
                        break;
                    case SDLK_b:
                        sidebarVisible_ = !sidebarVisible_;
                        break;
                    case SDLK_q:
                        running_ = false;
                        break;
                    default:
                        break;
                }
            }
        }

        // Only handle mouse/keyboard if ImGui doesn't want capture
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantCaptureMouse) {
            // Check navigation lock before processing mouse input
            if (IsNavigationLocked()) {
                // Block ALL mouse navigation when locked
                continue;  // Skip mouse input
            }

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

    // Check if navigation lock has expired
    CheckLockExpiry();

    // Process IPC messages (non-blocking, max 10ms per frame for 60 FPS)
    if (ipcServer_) {
        ipcServer_->ProcessMessages(10);
    }
}

void Application::Update() {
    // Update viewport animation
    if (viewport_) {
        double currentTimeMs = static_cast<double>(SDL_GetTicks());
        viewport_->UpdateAnimation(currentTimeMs);

        // Track animation completion for tokens
        for (auto& [key, token] : activeAnimations_) {
            if (!token.completed && !token.aborted) {
                if (!viewport_->animation_.IsActive()) {
                    token.completed = true;
                    token.finalPosition = viewport_->GetPosition();
                    token.finalZoom = viewport_->GetZoom();
                }
            }
        }

        // Cleanup expired tokens (prevent memory leak)
        auto now = std::chrono::steady_clock::now();
        for (auto it = activeAnimations_.begin(); it != activeAnimations_.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.createdAt
            );
            if (age.count() > MAX_TOKEN_AGE_MS) {
                it = activeAnimations_.erase(it);
            } else {
                ++it;
            }
        }
    }
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
        minimap_->Render(*viewport_, sidebarVisible_, sidebarVisible_ ? SIDEBAR_WIDTH : 0.0f);
    }

    // Capture screenshot if requested
    if (screenshotBuffer_->IsCaptureRequested()) {
        CaptureScreenshot();
        screenshotBuffer_->ClearCaptureRequest();
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
    RenderWelcomeOverlay();

    // Render navigation lock indicator (overlay)
    if (IsNavigationLocked()) {
        RenderNavigationLockIndicator();
    }
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
    slideRenderer_->Initialize();  // Start async tile loading threads

    // Create minimap
    int minimapHeight = std::max(0, windowHeight_ - static_cast<int>(STATUS_BAR_HEIGHT));
    minimap_ = std::make_unique<Minimap>(
        slideLoader_.get(),
        renderer_,
        windowWidth_,
        minimapHeight
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

void Application::RenderNavigationLockIndicator() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##NavLockIndicator", nullptr, flags)) {
        ImGui::PushFont(fontMedium_);
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "NAVIGATION LOCKED");
        ImGui::PopFont();

        ImGui::Spacing();

        auto timeRemaining = navLock_->GetTTL() -
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - navLock_->GetGrantedTime()
            );

        int remainingSeconds = timeRemaining.count() / 1000;
        ImGui::Text("Owner: %.8s...", navLock_->GetOwnerUUID().c_str());
        ImGui::Text("Time: %d:%02d", remainingSeconds / 60, remainingSeconds % 60);

        // TODO: Force release button (deferred to later step)
    }
    ImGui::End();
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
        const float buttonHeight = TOOLBAR_HEIGHT - 10.0f;
        const ImVec2 buttonSize(150.0f, buttonHeight);

        const char* sidebarLabel = sidebarVisible_ ? ICON_FA_EYE_SLASH "  Hide Sidebar"
                                                   : ICON_FA_EYE "  Show Sidebar";
        if (ImGui::Button(sidebarLabel, buttonSize)) {
            sidebarVisible_ = !sidebarVisible_;
        }
        ImGui::SameLine();

        if (viewport_) {
            if (ImGui::Button(ICON_FA_CROSSHAIRS "  Reset View", buttonSize)) {
                viewport_->ResetView();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button(ICON_FA_CROSSHAIRS "  Reset View", buttonSize);
            ImGui::EndDisabled();
        }
        ImGui::SameLine();

        // Polygon tool button (toggle style)
        bool wasActive = annotationManager_ && annotationManager_->IsToolActive();

        if (wasActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        }

        if (ImGui::Button(ICON_FA_DRAW_POLYGON "  Polygon Tool", buttonSize)) {
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

            // Tab 4: Action Cards
            if (ImGui::BeginTabItem("Action Cards")) {
                RenderActionCardsTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void Application::RenderWelcomeOverlay() {
    if (slideLoader_ && slideLoader_->IsValid()) {
        return;
    }

    ImGui::SetNextWindowPos(
        ImVec2(windowWidth_ * 0.5f, windowHeight_ * 0.5f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(420.0f, 0.0f),
        ImVec2(420.0f, std::numeric_limits<float>::max()));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##WelcomeOverlay", nullptr, flags)) {
        ImGui::Text("Welcome to PathView");
        ImGui::Separator();
        ImGui::TextWrapped("Load a whole-slide image to explore it with high-resolution zoom and pan.");
        ImGui::Spacing();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open Slide (Ctrl+O)", ImVec2(-FLT_MIN, 0.0f))) {
            OpenFileDialog();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Quick tips");
        ImGui::BulletText("Mouse wheel to zoom, click + drag to pan");
        ImGui::BulletText("Use the minimap to jump to regions of interest");
        ImGui::BulletText("Load polygon data to see AI-detected cells overlaid");
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
            std::string className = polygonOverlay_->GetClassName(classId);
            if (ImGui::ColorEdit3(className.c_str(),
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

void Application::RenderActionCardsTab() {
    std::lock_guard<std::mutex> lock(actionCardsMutex_);

    if (actionCards_.empty()) {
        // Empty state
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "No action cards");
        ImGui::Spacing();
        ImGui::TextWrapped("Action cards will appear here when an AI agent "
                          "performs tasks via the MCP interface.");
        return;
    }

    // Render cards in reverse chronological order (newest first)
    for (auto it = actionCards_.rbegin(); it != actionCards_.rend(); ++it) {
        const auto& card = *it;

        // Card header with colored status indicator
        ImGui::PushID(card.id.c_str());

        // Status icon and color
        ImVec4 statusColor;
        const char* statusIcon;
        switch (card.status) {
            case pathview::ActionCardStatus::PENDING:
                statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                statusIcon = ICON_FA_CIRCLE;
                break;
            case pathview::ActionCardStatus::IN_PROGRESS:
                statusColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
                statusIcon = ICON_FA_SPINNER;
                break;
            case pathview::ActionCardStatus::COMPLETED:
                statusColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
                statusIcon = ICON_FA_CHECK_CIRCLE;
                break;
            case pathview::ActionCardStatus::FAILED:
                statusColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                statusIcon = ICON_FA_TIMES_CIRCLE;
                break;
            case pathview::ActionCardStatus::CANCELLED:
                statusColor = ImVec4(0.8f, 0.6f, 0.2f, 1.0f);
                statusIcon = ICON_FA_BAN;
                break;
        }

        // Card container
        ImGui::BeginChild(("card_" + card.id).c_str(),
                         ImVec2(0, 0), true,
                         ImGuiWindowFlags_AlwaysAutoResize);

        // Title row
        ImGui::TextColored(statusColor, "%s", statusIcon);
        ImGui::SameLine();
        ImGui::PushFont(fontMedium_);
        ImGui::Text("%s", card.title.c_str());
        ImGui::PopFont();

        // Status and owner
        ImGui::Text("Status: %s",
                   pathview::ActionCard::StatusToString(card.status).c_str());
        if (!card.ownerUUID.empty()) {
            ImGui::Text("Owner: %.8s...", card.ownerUUID.c_str());

            // Show linkage to current lock
            if (navLock_->IsLocked() &&
                navLock_->GetOwnerUUID() == card.ownerUUID) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                  ICON_FA_LOCK " (Active Lock)");
            }
        }

        // Summary
        if (!card.summary.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", card.summary.c_str());
        }

        // Collapsible reasoning section
        if (!card.reasoning.empty()) {
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Reasoning",
                                       ImGuiTreeNodeFlags_None)) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                     ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                ImGui::TextWrapped("%s", card.reasoning.c_str());
                ImGui::PopStyleColor();
            }
        }

        // Log entries
        if (!card.logEntries.empty()) {
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Activity Log",
                                       ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginChild(("log_" + card.id).c_str(),
                                 ImVec2(0, 150), true);

                for (const auto& entry : card.logEntries) {
                    // Timestamp
                    auto time_t = std::chrono::system_clock::to_time_t(
                        entry.timestamp);
                    struct tm* tm = std::localtime(&time_t);
                    char timeBuf[32];
                    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", tm);

                    // Level color
                    ImVec4 levelColor;
                    if (entry.level == "error") {
                        levelColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    } else if (entry.level == "warning") {
                        levelColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
                    } else if (entry.level == "success") {
                        levelColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
                    } else {
                        levelColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    }

                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                      "[%s]", timeBuf);
                    ImGui::SameLine();
                    ImGui::TextColored(levelColor, "%s", entry.message.c_str());
                }

                // Auto-scroll to bottom on new entries
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }

                ImGui::EndChild();
            }
        }

        // Timestamps
        ImGui::Separator();
        auto created = std::chrono::system_clock::to_time_t(card.createdAt);
        auto updated = std::chrono::system_clock::to_time_t(card.updatedAt);
        struct tm* tmCreated = std::localtime(&created);
        struct tm* tmUpdated = std::localtime(&updated);
        char createdBuf[64], updatedBuf[64];
        strftime(createdBuf, sizeof(createdBuf), "%Y-%m-%d %H:%M:%S", tmCreated);
        strftime(updatedBuf, sizeof(updatedBuf), "%Y-%m-%d %H:%M:%S", tmUpdated);

        ImGui::Text("Created: %s", createdBuf);
        ImGui::Text("Updated: %s", updatedBuf);

        ImGui::EndChild();

        ImGui::PopID();
        ImGui::Spacing();
    }
}

// IPC command handler
pathview::ipc::json Application::HandleIPCCommand(const std::string& method, const pathview::ipc::json& params) {
    using namespace pathview::ipc;

    try {
        // Viewport commands
        if (method == "viewport.pan") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Check navigation lock
            socket_t currentClientFd = ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE;
            if (!IsNavigationOwnedByClient(currentClientFd)) {
                throw std::runtime_error(
                    std::string("Navigation locked by ") + navLock_->GetOwnerUUID() +
                    ". Use nav_lock tool to acquire control."
                );
            }

            double dx = params.at("dx").get<double>();
            double dy = params.at("dy").get<double>();
            viewport_->Pan(Vec2(dx, dy), AnimationMode::SMOOTH);

            Vec2 pos = viewport_->GetPosition();
            return json{
                {"position", {{"x", pos.x}, {"y", pos.y}}},
                {"zoom", viewport_->GetZoom()}
            };
        }
        else if (method == "viewport.zoom") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Check navigation lock
            socket_t currentClientFd = ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE;
            if (!IsNavigationOwnedByClient(currentClientFd)) {
                throw std::runtime_error(
                    std::string("Navigation locked by ") + navLock_->GetOwnerUUID() +
                    ". Use nav_lock tool to acquire control."
                );
            }

            double delta = params.at("delta").get<double>();

            // Zoom at center of viewport
            Vec2 center(windowWidth_ / 2.0, windowHeight_ / 2.0);
            viewport_->ZoomAtPoint(center, delta, AnimationMode::SMOOTH);

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
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Check navigation lock
            socket_t currentClientFd = ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE;
            if (!IsNavigationOwnedByClient(currentClientFd)) {
                throw std::runtime_error(
                    std::string("Navigation locked by ") + navLock_->GetOwnerUUID() +
                    ". Use nav_lock tool to acquire control."
                );
            }

            int screenX = params.at("screen_x").get<int>();
            int screenY = params.at("screen_y").get<int>();
            double delta = params.at("delta").get<double>();

            viewport_->ZoomAtPoint(Vec2(screenX, screenY), delta, AnimationMode::SMOOTH);

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
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Check navigation lock
            socket_t currentClientFd = ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE;
            if (!IsNavigationOwnedByClient(currentClientFd)) {
                throw std::runtime_error(
                    std::string("Navigation locked by ") + navLock_->GetOwnerUUID() +
                    ". Use nav_lock tool to acquire control."
                );
            }

            double x = params.at("x").get<double>();
            double y = params.at("y").get<double>();
            viewport_->CenterOn(Vec2(x, y), AnimationMode::SMOOTH);

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
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Check navigation lock
            socket_t currentClientFd = ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE;
            if (!IsNavigationOwnedByClient(currentClientFd)) {
                throw std::runtime_error(
                    std::string("Navigation locked by ") + navLock_->GetOwnerUUID() +
                    ". Use nav_lock tool to acquire control."
                );
            }

            viewport_->ResetView(AnimationMode::SMOOTH);

            return json{
                {"position", {
                    {"x", viewport_->GetPosition().x},
                    {"y", viewport_->GetPosition().y}
                }},
                {"zoom", viewport_->GetZoom()}
            };
        }
        else if (method == "viewport.move") {
            if (!viewport_) {
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Check navigation lock
            socket_t currentClientFd = ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE;
            if (!IsNavigationOwnedByClient(currentClientFd)) {
                throw std::runtime_error(
                    std::string("Navigation locked by ") + navLock_->GetOwnerUUID() +
                    ". Use nav_lock tool to acquire control."
                );
            }

            // Parse parameters
            double centerX = params.at("center_x").get<double>();
            double centerY = params.at("center_y").get<double>();
            double zoom = params.at("zoom").get<double>();
            double durationMs = params.value("duration_ms", 300.0);

            // Clamp duration to reasonable range
            durationMs = std::clamp(durationMs, 50.0, 5000.0);

            // Abort any existing tracked animation
            for (auto& pair : activeAnimations_) {
                if (!pair.second.completed && !pair.second.aborted) {
                    pair.second.aborted = true;
                    pair.second.completed = true;
                    pair.second.finalPosition = viewport_->GetPosition();
                    pair.second.finalZoom = viewport_->GetZoom();
                }
            }

            // Calculate target position (center to top-left)
            double viewportWidth = windowWidth_ / zoom;
            double viewportHeight = windowHeight_ / zoom;
            Vec2 targetPos(centerX - viewportWidth / 2.0,
                           centerY - viewportHeight / 2.0);

            // Generate token
            std::string token = GenerateUUID();

            // Start animation using Animation::StartAt
            double currentTime = static_cast<double>(SDL_GetTicks());
            viewport_->animation_.StartAt(
                viewport_->GetPosition(), viewport_->GetZoom(),
                targetPos, zoom,
                AnimationMode::SMOOTH,
                currentTime,
                durationMs
            );

            // Clamp final target to bounds (animation will lerp to clamped values)
            viewport_->ClampToBounds();

            // Track animation
            pathview::AnimationToken animToken;
            animToken.token = token;
            animToken.completed = false;
            animToken.aborted = false;
            animToken.finalPosition = viewport_->GetPosition();  // After clamp
            animToken.finalZoom = viewport_->GetZoom();
            animToken.createdAt = std::chrono::steady_clock::now();
            activeAnimations_[token] = animToken;

            return json{{"token", token}};
        }
        else if (method == "viewport.await_move") {
            std::string token = params.at("token").get<std::string>();

            auto it = activeAnimations_.find(token);
            if (it == activeAnimations_.end()) {
                throw std::runtime_error("Unknown animation token: " + token);
            }

            const pathview::AnimationToken& animToken = it->second;

            return json{
                {"completed", animToken.completed},
                {"aborted", animToken.aborted},
                {"position", {
                    {"x", animToken.finalPosition.x},
                    {"y", animToken.finalPosition.y}
                }},
                {"zoom", animToken.finalZoom}
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
                throw std::runtime_error("No polygons loaded. Use load_polygons tool to load cell segmentation data first.");
            }

            bool visible = params.at("visible").get<bool>();
            polygonOverlay_->SetVisible(visible);

            return json{{"visible", polygonOverlay_->IsVisible()}};
        }
        else if (method == "polygons.query") {
            if (!polygonOverlay_) {
                throw std::runtime_error("No polygons loaded. Use load_polygons tool to load cell segmentation data first.");
            }

            double x = params.at("x").get<double>();
            double y = params.at("y").get<double>();
            double w = params.at("w").get<double>();
            double h = params.at("h").get<double>();

            // TODO: Implement polygon query in PolygonOverlay
            // For now, return empty list
            return json{{"polygons", json::array()}};
        }

        // Session commands
        else if (method == "session.hello") {
            std::string agentName = params.value("agent_name", "unknown");
            std::string agentVersion = params.value("agent_version", "");
            std::string sessionId = params.value("session_id", "");

            // Log agent connection
            std::cout << "Agent connected: " << agentName
                      << " v" << agentVersion
                      << " (session: " << sessionId << ")" << std::endl;

            // Return session info including lock status
            json result = {
                {"session_id", sessionId},
                {"agent_name", agentName},
                {"pathview_version", "0.1.0"},
                {"mcp_server_url", "http://127.0.0.1:9000"},
                {"http_server_url", "http://127.0.0.1:8080"},
                {"stream_url", "http://127.0.0.1:8080/stream"},
                {"stream_fps_default", 5},
                {"stream_fps_max", 30},
                {"ipc_port", ipcServer_ ? ipcServer_->GetPort() : 0},
                {"navigation_locked", IsNavigationLocked()},
                {"lock_owner", navLock_->IsLocked() ? navLock_->GetOwnerUUID() : ""}
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

            if (slideLoader_) {
                result["slide"] = {
                    {"width", slideLoader_->GetWidth()},
                    {"height", slideLoader_->GetHeight()},
                    {"levels", slideLoader_->GetLevelCount()},
                    {"path", currentSlidePath_}
                };
            }

            return result;
        }

        // Navigation lock commands
        else if (method == "nav.lock") {
            std::string ownerUUID = params.value("owner_uuid", "");
            int ttlSeconds = params.value("ttl_seconds", 300);  // Default 5 minutes

            if (ownerUUID.empty()) {
                throw std::runtime_error("Missing 'owner_uuid' parameter");
            }

            // Enforce maximum TTL of 1 hour
            const int MAX_TTL_SECONDS = 3600;
            if (ttlSeconds > MAX_TTL_SECONDS) {
                ttlSeconds = MAX_TTL_SECONDS;
            }

            // Check if already locked by another owner
            if (navLock_->IsLocked() && navLock_->GetOwnerUUID() != ownerUUID && !navLock_->IsExpired()) {
                auto timeRemaining = navLock_->GetTTL() -
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - navLock_->GetGrantedTime()
                    );

                return json{
                    {"success", false},
                    {"error", "Navigation already locked by another agent"},
                    {"lock_owner", navLock_->GetOwnerUUID()},
                    {"time_remaining_ms", timeRemaining.count()}
                };
            }

            // Grant or renew lock
            navLock_->SetLocked(true);
            navLock_->SetOwnerUUID(ownerUUID);
            navLock_->SetGrantedTime(std::chrono::steady_clock::now());
            navLock_->SetTTL(std::chrono::milliseconds(ttlSeconds * 1000));
            navLock_->SetClientFd(ipcServer_ ? ipcServer_->GetCurrentClientFd() : INVALID_SOCKET_VALUE);

            std::cout << "Navigation lock granted to " << ownerUUID
                      << " for " << ttlSeconds << "s" << std::endl;

            return json{
                {"success", true},
                {"lock_owner", navLock_->GetOwnerUUID()},
                {"granted_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                    navLock_->GetGrantedTime().time_since_epoch()
                ).count()},
                {"ttl_ms", navLock_->GetTTL().count()}
            };
        }
        else if (method == "nav.unlock") {
            std::string ownerUUID = params.value("owner_uuid", "");

            if (ownerUUID.empty()) {
                throw std::runtime_error("Missing 'owner_uuid' parameter");
            }

            // Check ownership
            if (!navLock_->IsOwnedBy(ownerUUID)) {
                if (!navLock_->IsLocked()) {
                    return json{
                        {"success", false},
                        {"error", "Navigation not locked"}
                    };
                } else {
                    return json{
                        {"success", false},
                        {"error", "Not the lock owner"},
                        {"lock_owner", navLock_->GetOwnerUUID()}
                    };
                }
            }

            std::cout << "Navigation lock released by " << ownerUUID << std::endl;
            navLock_->Reset();

            return json{
                {"success", true},
                {"message", "Navigation unlocked"}
            };
        }
        else if (method == "nav.lock_status") {
            if (!navLock_->IsLocked() || navLock_->IsExpired()) {
                return json{
                    {"locked", false}
                };
            }

            auto timeRemaining = navLock_->GetTTL() -
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - navLock_->GetGrantedTime()
                );

            return json{
                {"locked", true},
                {"owner_uuid", navLock_->GetOwnerUUID()},
                {"time_remaining_ms", timeRemaining.count()},
                {"granted_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                    navLock_->GetGrantedTime().time_since_epoch()
                ).count()}
            };
        }
        else if (method == "snapshot.capture") {
            // Optional parameters
            bool includeUI = params.value("include_ui", false);
            int width = params.value("width", windowWidth_);
            int height = params.value("height", windowHeight_);

            // Note: includeUI and custom width/height not yet implemented
            // Currently captures at window resolution without UI

            // Capture immediately.
            //
            // Important: IPC requests are processed on the GUI thread (via IPCServer::ProcessMessages).
            // Waiting for "next frame" would deadlock rendering, so we read pixels from the last-rendered
            // frame synchronously.
            CaptureScreenshot();

            // Get captured data and encode PNG
            std::vector<uint8_t> pixels;
            int capturedWidth, capturedHeight;
            if (!screenshotBuffer_->GetCapture(pixels, capturedWidth, capturedHeight)) {
                return json{{"error", "Failed to capture screenshot"}};
            }

            std::vector<uint8_t> pngData = EncodePNG(pixels, capturedWidth, capturedHeight);
            screenshotBuffer_->MarkAsRead();

            // Return PNG data as base64 for MCP server to store
            // Convert to base64
            static const char* base64_chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789+/";

            std::string base64;
            int i = 0;
            unsigned char char_array_3[3];
            unsigned char char_array_4[4];

            for (size_t idx = 0; idx < pngData.size(); idx++) {
                char_array_3[i++] = pngData[idx];
                if (i == 3) {
                    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                    char_array_4[3] = char_array_3[2] & 0x3f;

                    for(i = 0; i < 4; i++)
                        base64 += base64_chars[char_array_4[i]];
                    i = 0;
                }
            }

            if (i) {
                for(int j = i; j < 3; j++)
                    char_array_3[j] = '\0';

                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

                for (int j = 0; j < i + 1; j++)
                    base64 += base64_chars[char_array_4[j]];

                while(i++ < 3)
                    base64 += '=';
            }

            return json{
                {"png_data", base64},
                {"width", capturedWidth},
                {"height", capturedHeight}
            };
        }
        else if (method == "annotations.create") {
            // Check if slide is loaded
            if (!slideLoader_) {
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Parse and validate vertices
            if (!params.contains("vertices")) {
                throw std::runtime_error("Missing 'vertices' parameter");
            }

            auto verticesJson = params["vertices"];
            if (!verticesJson.is_array() || verticesJson.size() < 3) {
                throw std::runtime_error("vertices must be an array with at least 3 points");
            }

            // Convert JSON vertices to Vec2 vector
            std::vector<Vec2> vertices;
            for (const auto& vertexJson : verticesJson) {
                if (!vertexJson.is_array() || vertexJson.size() != 2) {
                    throw std::runtime_error("Each vertex must be an array of [x, y]");
                }
                vertices.push_back(Vec2(vertexJson[0].get<double>(),
                                       vertexJson[1].get<double>()));
            }

            // Get optional name
            std::string name = params.value("name", "");

            // Create annotation
            int id = annotationManager_->CreateAnnotation(vertices, name,
                                                         polygonOverlay_.get());

            if (id < 0) {
                throw std::runtime_error("Failed to create annotation (invalid vertices)");
            }

            // Get the created annotation
            const auto* annotation = annotationManager_->GetAnnotationById(id);
            if (!annotation) {
                throw std::runtime_error("Failed to retrieve created annotation");
            }

            // Compute area and build response
            double area = AnnotationManager::ComputeArea(annotation->vertices);

            json cellCountsJson = json::object();
            int totalCells = 0;
            for (const auto& [classId, count] : annotation->cellCounts) {
                cellCountsJson[std::to_string(classId)] = count;
                totalCells += count;
            }
            if (!annotation->cellCounts.empty()) {
                cellCountsJson["total"] = totalCells;
            }

            json response = {
                {"id", annotation->id},
                {"name", annotation->name},
                {"vertex_count", annotation->vertices.size()},
                {"bounding_box", {
                    {"x", annotation->boundingBox.x},
                    {"y", annotation->boundingBox.y},
                    {"width", annotation->boundingBox.width},
                    {"height", annotation->boundingBox.height}
                }},
                {"area", area},
                {"cell_counts", cellCountsJson}
            };

            // Add warning if polygons not loaded
            if (!polygonOverlay_ || polygonOverlay_->GetPolygonCount() == 0) {
                response["warning"] = "No polygons loaded. Cell counts unavailable. Use load_polygons to enable cell counting.";
            }

            return response;
        }
        else if (method == "annotations.list") {
            // Check if slide is loaded
            if (!slideLoader_) {
                throw std::runtime_error("No slide loaded");
            }

            bool includeMetrics = params.value("include_metrics", false);

            const auto& annotations = annotationManager_->GetAnnotations();
            json annotationsJson = json::array();

            for (const auto& annotation : annotations) {
                double area = AnnotationManager::ComputeArea(annotation.vertices);

                json annotationJson = {
                    {"id", annotation.id},
                    {"name", annotation.name},
                    {"vertex_count", annotation.vertices.size()},
                    {"bounding_box", {
                        {"x", annotation.boundingBox.x},
                        {"y", annotation.boundingBox.y},
                        {"width", annotation.boundingBox.width},
                        {"height", annotation.boundingBox.height}
                    }},
                    {"area", area}
                };

                if (includeMetrics) {
                    json cellCountsJson = json::object();
                    int totalCells = 0;
                    for (const auto& [classId, count] : annotation.cellCounts) {
                        cellCountsJson[std::to_string(classId)] = count;
                        totalCells += count;
                    }
                    if (!annotation.cellCounts.empty()) {
                        cellCountsJson["total"] = totalCells;
                    }
                    annotationJson["cell_counts"] = cellCountsJson;
                }

                annotationsJson.push_back(annotationJson);
            }

            return json{
                {"annotations", annotationsJson},
                {"count", annotations.size()}
            };
        }
        else if (method == "annotations.get") {
            // Check if slide is loaded
            if (!slideLoader_) {
                throw std::runtime_error("No slide loaded");
            }

            // Parse and validate id parameter
            if (!params.contains("id")) {
                throw std::runtime_error("Missing 'id' parameter");
            }

            int id = params["id"].get<int>();

            // Get annotation by ID
            const auto* annotation = annotationManager_->GetAnnotationById(id);
            if (!annotation) {
                throw std::runtime_error("Annotation with id " + std::to_string(id) + " not found");
            }

            // Convert vertices to JSON
            json verticesJson = json::array();
            for (const auto& vertex : annotation->vertices) {
                verticesJson.push_back({vertex.x, vertex.y});
            }

            // Compute metrics
            double area = AnnotationManager::ComputeArea(annotation->vertices);
            double perimeter = AnnotationManager::ComputePerimeter(annotation->vertices);

            json cellCountsJson = json::object();
            int totalCells = 0;
            for (const auto& [classId, count] : annotation->cellCounts) {
                cellCountsJson[std::to_string(classId)] = count;
                totalCells += count;
            }
            if (!annotation->cellCounts.empty()) {
                cellCountsJson["total"] = totalCells;
            }

            return json{
                {"id", annotation->id},
                {"name", annotation->name},
                {"vertices", verticesJson},
                {"vertex_count", annotation->vertices.size()},
                {"bounding_box", {
                    {"x", annotation->boundingBox.x},
                    {"y", annotation->boundingBox.y},
                    {"width", annotation->boundingBox.width},
                    {"height", annotation->boundingBox.height}
                }},
                {"area", area},
                {"perimeter", perimeter},
                {"cell_counts", cellCountsJson}
            };
        }
        else if (method == "annotations.delete") {
            // Check if slide is loaded
            if (!slideLoader_) {
                throw std::runtime_error("No slide loaded");
            }

            // Parse and validate id parameter
            if (!params.contains("id")) {
                throw std::runtime_error("Missing 'id' parameter");
            }

            int id = params["id"].get<int>();

            // Delete annotation by ID
            bool deleted = annotationManager_->DeleteAnnotationById(id);
            if (!deleted) {
                throw std::runtime_error("Annotation with id " + std::to_string(id) + " not found");
            }

            return json{
                {"success", true},
                {"deleted_id", id}
            };
        }
        else if (method == "annotations.compute_metrics") {
            // Check if slide is loaded
            if (!slideLoader_) {
                throw std::runtime_error("No slide loaded. Use load_slide tool to load a whole-slide image first.");
            }

            // Parse and validate vertices
            if (!params.contains("vertices")) {
                throw std::runtime_error("Missing 'vertices' parameter");
            }

            auto verticesJson = params["vertices"];
            if (!verticesJson.is_array() || verticesJson.size() < 3) {
                throw std::runtime_error("vertices must be an array with at least 3 points");
            }

            // Convert JSON vertices to Vec2 vector
            std::vector<Vec2> vertices;
            for (const auto& vertexJson : verticesJson) {
                if (!vertexJson.is_array() || vertexJson.size() != 2) {
                    throw std::runtime_error("Each vertex must be an array of [x, y]");
                }
                vertices.push_back(Vec2(vertexJson[0].get<double>(),
                                       vertexJson[1].get<double>()));
            }

            // Compute metrics without creating annotation
            auto metrics = annotationManager_->ComputeMetricsForVertices(vertices,
                                                                        polygonOverlay_.get());

            json cellCountsJson = json::object();
            for (const auto& [classId, count] : metrics.cellCounts) {
                cellCountsJson[std::to_string(classId)] = count;
            }
            if (!metrics.cellCounts.empty()) {
                cellCountsJson["total"] = metrics.totalCells;
            }

            json response = {
                {"bounding_box", {
                    {"x", metrics.boundingBox.x},
                    {"y", metrics.boundingBox.y},
                    {"width", metrics.boundingBox.width},
                    {"height", metrics.boundingBox.height}
                }},
                {"area", metrics.area},
                {"perimeter", metrics.perimeter},
                {"cell_counts", cellCountsJson}
            };

            // Add warning if polygons not loaded
            if (!polygonOverlay_ || polygonOverlay_->GetPolygonCount() == 0) {
                response["warning"] = "No polygons loaded. Cell counts unavailable. Use load_polygons to enable cell counting.";
            }

            return response;
        }

        // Action card commands
        else if (method == "action_card.create") {
            std::string title = params.at("title").get<std::string>();
            std::string summary = params.value("summary", "");
            std::string reasoning = params.value("reasoning", "");
            std::string ownerUUID = params.value("owner_uuid", "");

            // Generate UUID
            std::string cardId = GenerateUUID();

            // Create card
            pathview::ActionCard card(cardId, title);
            card.summary = summary;
            card.reasoning = reasoning;
            card.ownerUUID = ownerUUID;

            // Store (thread-safe)
            {
                std::lock_guard<std::mutex> lock(actionCardsMutex_);

                // Enforce max cards limit
                if (actionCards_.size() >= MAX_ACTION_CARDS) {
                    // Remove oldest completed/failed/cancelled card
                    auto it = std::find_if(actionCards_.begin(), actionCards_.end(),
                        [](const pathview::ActionCard& c) {
                            return c.status == pathview::ActionCardStatus::COMPLETED ||
                                   c.status == pathview::ActionCardStatus::FAILED ||
                                   c.status == pathview::ActionCardStatus::CANCELLED;
                        });
                    if (it != actionCards_.end()) {
                        actionCards_.erase(it);
                    }
                }

                actionCards_.push_back(card);
            }

            std::cout << "Action card created: " << title << " (id: " << cardId << ")" << std::endl;

            return json{
                {"id", cardId},
                {"title", title},
                {"status", pathview::ActionCard::StatusToString(card.status)},
                {"created_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                    card.createdAt.time_since_epoch()).count()}
            };
        }
        else if (method == "action_card.update") {
            std::string cardId = params.at("id").get<std::string>();

            std::lock_guard<std::mutex> lock(actionCardsMutex_);

            // Find card
            auto it = std::find_if(actionCards_.begin(), actionCards_.end(),
                [&cardId](const pathview::ActionCard& c) { return c.id == cardId; });

            if (it == actionCards_.end()) {
                throw std::runtime_error("Action card not found: " + cardId);
            }

            // Update fields
            if (params.contains("status")) {
                std::string statusStr = params["status"].get<std::string>();
                it->UpdateStatus(pathview::ActionCard::StringToStatus(statusStr));
            }
            if (params.contains("summary")) {
                it->summary = params["summary"].get<std::string>();
                it->updatedAt = std::chrono::system_clock::now();
            }
            if (params.contains("reasoning")) {
                it->reasoning = params["reasoning"].get<std::string>();
                it->updatedAt = std::chrono::system_clock::now();
            }

            return json{
                {"id", it->id},
                {"status", pathview::ActionCard::StatusToString(it->status)},
                {"updated_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                    it->updatedAt.time_since_epoch()).count()}
            };
        }
        else if (method == "action_card.append_log") {
            std::string cardId = params.at("id").get<std::string>();
            std::string message = params.at("message").get<std::string>();
            std::string level = params.value("level", "info");

            std::lock_guard<std::mutex> lock(actionCardsMutex_);

            // Find card
            auto it = std::find_if(actionCards_.begin(), actionCards_.end(),
                [&cardId](const pathview::ActionCard& c) { return c.id == cardId; });

            if (it == actionCards_.end()) {
                throw std::runtime_error("Action card not found: " + cardId);
            }

            // Append log
            it->AppendLog(message, level);

            return json{
                {"id", it->id},
                {"log_count", it->logEntries.size()},
                {"updated_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                    it->updatedAt.time_since_epoch()).count()}
            };
        }
        else if (method == "action_card.list") {
            std::lock_guard<std::mutex> lock(actionCardsMutex_);

            json cardsJson = json::array();
            for (const auto& card : actionCards_) {
                cardsJson.push_back({
                    {"id", card.id},
                    {"title", card.title},
                    {"status", pathview::ActionCard::StatusToString(card.status)},
                    {"summary", card.summary},
                    {"owner_uuid", card.ownerUUID},
                    {"log_entry_count", card.logEntries.size()},
                    {"created_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                        card.createdAt.time_since_epoch()).count()},
                    {"updated_at", std::chrono::duration_cast<std::chrono::milliseconds>(
                        card.updatedAt.time_since_epoch()).count()}
                });
            }

            return json{
                {"cards", cardsJson},
                {"count", actionCards_.size()}
            };
        }
        else if (method == "action_card.delete") {
            std::string cardId = params.at("id").get<std::string>();

            std::lock_guard<std::mutex> lock(actionCardsMutex_);

            auto it = std::find_if(actionCards_.begin(), actionCards_.end(),
                [&cardId](const pathview::ActionCard& c) { return c.id == cardId; });

            if (it == actionCards_.end()) {
                throw std::runtime_error("Action card not found: " + cardId);
            }

            actionCards_.erase(it);

            return json{
                {"success", true},
                {"deleted_id", cardId}
            };
        }

        // Unknown method
        throw std::runtime_error("Unknown method: " + method);

    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("JSON error: ") + e.what());
    }
}

void Application::CaptureScreenshot() {
    int w = windowWidth_;
    int h = windowHeight_;

    // Allocate buffer for RGBA pixels
    std::vector<uint8_t> pixels(w * h * 4);

    // Read pixels from renderer (MUST be on render thread)
    SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA8888,
                        pixels.data(), w * 4);

    // Store in buffer (thread-safe)
    screenshotBuffer_->StoreCapture(pixels, w, h);
}

std::vector<uint8_t> Application::EncodePNG(const std::vector<uint8_t>& pixels,
                                            int width, int height) {
    return pathview::PNGEncoder::Encode(pixels, width, height);
}
