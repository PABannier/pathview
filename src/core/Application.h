#pragma once

#include <SDL2/SDL.h>
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <mutex>

// Cross-platform socket type (must be defined before NavigationLock.h if not included)
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
#endif

class SlideLoader;
class SlideRenderer;
class Minimap;
class Viewport;
class TextureManager;
class PolygonOverlay;
class AnnotationManager;
class NavigationLock;
struct ImFont;

namespace pathview {
class ScreenshotBuffer;
}

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

#include "AnimationToken.h"
#include "ActionCard.h"

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
    void RenderActionCardsTab();
    void RenderNavigationLockIndicator();

    // Navigation lock helpers
    bool IsNavigationLocked() const;
    bool IsNavigationOwnedByClient(socket_t clientFd) const;
    void CheckLockExpiry();
    std::string GenerateUUID() const;

    // Screenshot capture
    void CaptureScreenshot();
    std::vector<uint8_t> EncodePNG(const std::vector<uint8_t>& pixels, int width, int height);

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
    std::unique_ptr<NavigationLock> navLock_;

    // Action cards state
    std::vector<pathview::ActionCard> actionCards_;
    std::mutex actionCardsMutex_;  // Thread-safe IPC access
    static constexpr int MAX_ACTION_CARDS = 50;

    // Animation tracking for completion detection
    std::map<std::string, pathview::AnimationToken> activeAnimations_;
    static constexpr int MAX_TOKEN_AGE_MS = 60000;  // 60 seconds

    // Screenshot capture state
    std::unique_ptr<pathview::ScreenshotBuffer> screenshotBuffer_;

    // Toolbar configuration
    static constexpr float TOOLBAR_HEIGHT = 40.0f;
    static constexpr float STATUS_BAR_HEIGHT = 28.0f;
};
