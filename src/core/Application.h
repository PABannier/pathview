#pragma once

#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <chrono>
#include <uuid/uuid.h>

class SlideLoader;
class SlideRenderer;
class Minimap;
class Viewport;
class TextureManager;
class PolygonOverlay;
class AnnotationManager;
struct ImFont;

// Forward declare IPC types
namespace pathview {
namespace ipc {
    class IPCServer;
}
}

#include "json.hpp"
namespace pathview {
namespace ipc {
    using json = nlohmann::json;
}
}

/**
 * Navigation lock state
 * Prevents user input (keyboard + mouse) when locked
 */
struct NavigationLock {
    bool isLocked;
    std::string ownerUUID;  // UUID as string (36 chars)
    std::chrono::steady_clock::time_point grantedTime;
    std::chrono::milliseconds ttlMs;
    int clientFd;  // IPC client file descriptor (-1 if none)

    NavigationLock()
        : isLocked(false)
        , ownerUUID("")
        , grantedTime()
        , ttlMs(0)
        , clientFd(-1)
    {}

    bool IsExpired() const {
        if (!isLocked) return false;
        auto now = std::chrono::steady_clock::now();
        return (now - grantedTime) >= ttlMs;
    }

    bool IsOwnedBy(const std::string& uuid) const {
        return isLocked && ownerUUID == uuid;
    }
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
    void ProcessEvents();
    void Update();
    void Render();
    void RenderUI();
    void RenderSlidePreview();

    void OpenFileDialog();
    void LoadSlide(const std::string& path);
    void OpenPolygonFileDialog();
    void LoadPolygons(const std::string& path);

    // IPC command handler
    pathview::ipc::json HandleIPCCommand(const std::string& method, const pathview::ipc::json& params);

    // UI rendering methods
    void RenderMenuBar();
    void RenderToolbar();
    void RenderSidebar();
    void RenderWelcomeOverlay();
    void RenderSlideInfoTab();
    void RenderPolygonTab();
    void RenderNavigationLockIndicator();

    // Navigation lock helpers
    bool IsNavigationLocked() const;
    void CheckLockExpiry();
    std::string GenerateUUID() const;

    // SDL objects
    SDL_Window* window_;
    SDL_Renderer* renderer_;

    // Font handles
    ImFont* fontRegular_;   // Inter 15px with FA merged
    ImFont* fontMedium_;    // Inter 16px for emphasis

    // Application state
    bool running_;
    bool isPanning_;
    int lastMouseX_;
    int lastMouseY_;
    int windowWidth_;
    int windowHeight_;
    float dpiScale_;  // High-DPI scale factor (drawable size / window size)

    // Frame timing for animations
    uint32_t lastFrameTime_;
    double deltaTime_;

    // Components
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<SlideLoader> slideLoader_;
    std::unique_ptr<Viewport> viewport_;
    std::unique_ptr<SlideRenderer> slideRenderer_;
    std::unique_ptr<Minimap> minimap_;
    std::unique_ptr<PolygonOverlay> polygonOverlay_;
    std::unique_ptr<AnnotationManager> annotationManager_;

    // IPC server for remote control
    std::unique_ptr<pathview::ipc::IPCServer> ipcServer_;

    // Preview texture (Phase 2 simple display)
    SDL_Texture* previewTexture_;

    // Current slide path
    std::string currentSlidePath_;

    // Sidebar configuration
    static constexpr float SIDEBAR_WIDTH = 350.0f;
    bool sidebarVisible_;

    // Navigation lock state
    NavigationLock navLock_;

    // Toolbar configuration
    static constexpr float TOOLBAR_HEIGHT = 40.0f;
    static constexpr float STATUS_BAR_HEIGHT = 28.0f;
};
