#pragma once

#include <cstdint>
#include <algorithm>
#include "Animation.h"  // Provides Vec2 and AnimationMode

struct Rect {
    double x, y, width, height;

    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(double x_, double y_, double w_, double h_) : x(x_), y(y_), width(w_), height(h_) {}

    double Left() const { return x; }
    double Right() const { return x + width; }
    double Top() const { return y; }
    double Bottom() const { return y + height; }

    bool Contains(double px, double py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    bool Intersects(const Rect& other) const {
        return !(x + width < other.x || other.x + other.width < x ||
                 y + height < other.y || other.y + other.height < y);
    }
};

class Viewport {
public:
    Viewport(int windowWidth, int windowHeight, int64_t slideWidth, int64_t slideHeight);
    ~Viewport() = default;

    // Friend declaration to allow Application access to animation_ and ClampToBounds()
    friend class Application;

    // Window size management
    void SetWindowSize(int width, int height);
    int GetWindowWidth() const { return windowWidth_; }
    int GetWindowHeight() const { return windowHeight_; }

    // Slide dimensions
    void SetSlideDimensions(int64_t width, int64_t height);
    int64_t GetSlideWidth() const { return slideWidth_; }
    int64_t GetSlideHeight() const { return slideHeight_; }

    // Camera control
    void ZoomAtPoint(Vec2 screenPoint, double zoomDelta,
                     AnimationMode mode = AnimationMode::INSTANT);
    void Pan(Vec2 deltaInSlideCoords,
             AnimationMode mode = AnimationMode::INSTANT);
    void CenterOn(Vec2 slidePoint,
                  AnimationMode mode = AnimationMode::INSTANT);
    void ResetView(AnimationMode mode = AnimationMode::INSTANT);

    // Animation update (called each frame)
    void UpdateAnimation(double currentTimeMs);

    // Coordinate transformations
    Vec2 ScreenToSlide(Vec2 screenPos) const;
    Vec2 SlideToScreen(Vec2 slidePos) const;

    // Viewport state queries
    double GetZoom() const { return zoom_; }
    Vec2 GetPosition() const { return position_; }
    Rect GetVisibleRegion() const;

    // Zoom limits
    double GetMinZoom() const { return minZoom_; }
    double GetMaxZoom() const { return maxZoom_; }

private:
    void ClampToBounds();
    void CalculateZoomLimits();

    // Window dimensions
    int windowWidth_;
    int windowHeight_;

    // Slide dimensions
    int64_t slideWidth_;
    int64_t slideHeight_;

    // Camera state
    Vec2 position_;  // Top-left corner in slide coordinates
    double zoom_;    // Current zoom level (1.0 = 100%)

    // Zoom limits
    double minZoom_;  // Entire slide fits in window
    double maxZoom_;  // Maximum zoom (e.g., 4x or 1:1 pixel)

    // Animation state
    Animation animation_;
};
