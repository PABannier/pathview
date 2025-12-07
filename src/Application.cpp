#include "Application.h"
#include "SlideLoader.h"
#include "TextureManager.h"
#include "Viewport.h"
#include "SlideRenderer.h"
#include "Minimap.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
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
    , previewTexture_(nullptr)
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
            // Handle keyboard shortcuts
            if (event.key.keysym.sym == SDLK_r && viewport_) {
                viewport_->ResetView();
            }
        }

        // Only handle mouse/keyboard if ImGui doesn't want capture
        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantCaptureMouse) {
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

    // Render minimap overlay
    if (slideLoader_ && viewport_ && minimap_) {
        minimap_->Render(*viewport_);
    }

    // Render ImGui
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);

    // Present
    SDL_RenderPresent(renderer_);
}

void Application::RenderUI() {
    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Slide...", "Ctrl+O")) {
                OpenFileDialog();
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

    // Show slide info panel
    if (slideLoader_ && slideLoader_->IsValid()) {
        ImGui::Begin("Slide Information");
        ImGui::Text("Slide: %s", currentSlidePath_.c_str());
        ImGui::Separator();
        ImGui::Text("Dimensions: %lld x %lld", slideLoader_->GetWidth(), slideLoader_->GetHeight());
        ImGui::Text("Levels: %d", slideLoader_->GetLevelCount());

        // Show viewport info if active
        if (viewport_) {
            ImGui::Separator();
            ImGui::Text("Zoom: %.1f%%", viewport_->GetZoom() * 100.0);
            auto pos = viewport_->GetPosition();
            ImGui::Text("Position: (%.0f, %.0f)", pos.x, pos.y);
            auto visible = viewport_->GetVisibleRegion();
            ImGui::Text("Visible: %.0fx%.0f", visible.width, visible.height);
        }

        // Show cache statistics if renderer is active
        if (slideRenderer_) {
            ImGui::Separator();
            ImGui::Text("Tile Cache:");
            ImGui::Text("  Tiles: %zu", slideRenderer_->GetCacheTileCount());
            ImGui::Text("  Memory: %.1f MB", slideRenderer_->GetCacheMemoryUsage() / (1024.0 * 1024.0));
            ImGui::Text("  Hit rate: %.1f%%", slideRenderer_->GetCacheHitRate() * 100.0);
        }

        ImGui::Separator();
        for (int i = 0; i < slideLoader_->GetLevelCount(); ++i) {
            auto dims = slideLoader_->GetLevelDimensions(i);
            double downsample = slideLoader_->GetLevelDownsample(i);
            ImGui::Text("  Level %d: %lld x %lld (%.1fx)", i, dims.width, dims.height, downsample);
        }
        ImGui::End();
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

    // Create minimap
    minimap_ = std::make_unique<Minimap>(
        slideLoader_.get(),
        renderer_,
        windowWidth_,
        windowHeight_
    );

    std::cout << "Viewport, renderer, and minimap created" << std::endl;
    std::cout << "===================\n" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  - Mouse wheel: Zoom in/out" << std::endl;
    std::cout << "  - Click + drag: Pan" << std::endl;
    std::cout << "  - Click on minimap: Jump to location" << std::endl;
    std::cout << "  - 'R' or View -> Reset View: Reset to fit" << std::endl;
    std::cout << "===================\n" << std::endl;
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
