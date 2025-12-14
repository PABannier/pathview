#include "Viewport.h"
#include <iostream>
#include <cmath>
#include <SDL_timer.h>

Viewport::Viewport(int windowWidth, int windowHeight, int64_t slideWidth, int64_t slideHeight)
    : windowWidth_(windowWidth)
    , windowHeight_(windowHeight)
    , slideWidth_(slideWidth)
    , slideHeight_(slideHeight)
    , position_(0.0, 0.0)
    , zoom_(1.0)
    , minZoom_(0.01)
    , maxZoom_(4.0)
{
    CalculateZoomLimits();
    ResetView();
}

void Viewport::SetWindowSize(int width, int height) {
    windowWidth_ = width;
    windowHeight_ = height;
    CalculateZoomLimits();
    ClampToBounds();
}

void Viewport::SetSlideDimensions(int64_t width, int64_t height) {
    slideWidth_ = width;
    slideHeight_ = height;
    CalculateZoomLimits();
    ResetView();
}

void Viewport::ZoomAtPoint(Vec2 screenPoint, double zoomDelta, AnimationMode mode) {
    // Convert screen point to slide coordinates before zoom
    Vec2 slidePoint = ScreenToSlide(screenPoint);

    // Calculate target zoom
    double targetZoom = zoom_ * zoomDelta;
    targetZoom = std::clamp(targetZoom, minZoom_, maxZoom_);

    // If zoom didn't change (hit limit), don't animate
    if (targetZoom == zoom_) {
        return;
    }

    // Calculate target position so slidePoint remains under screenPoint
    Vec2 targetPos = slidePoint - screenPoint / targetZoom;

    // Pre-clamp target position
    Vec2 savedPos = position_;
    double savedZoom = zoom_;
    position_ = targetPos;
    zoom_ = targetZoom;
    ClampToBounds();
    targetPos = position_;
    targetZoom = zoom_;
    position_ = savedPos;
    zoom_ = savedZoom;

    // Start animation with specified mode
    animation_.Start(position_, zoom_, targetPos, targetZoom, mode, 300.0);
}

void Viewport::Pan(Vec2 deltaInSlideCoords, AnimationMode mode) {
    Vec2 targetPos = position_ + deltaInSlideCoords;

    // Pre-clamp target position
    Vec2 savedPos = position_;
    position_ = targetPos;
    ClampToBounds();
    targetPos = position_;
    position_ = savedPos;

    // Start animation with specified mode
    animation_.Start(position_, zoom_, targetPos, zoom_, mode, 300.0);
}

void Viewport::CenterOn(Vec2 slidePoint, AnimationMode mode) {
    // Calculate viewport size at CURRENT zoom
    double viewportWidth = windowWidth_ / zoom_;
    double viewportHeight = windowHeight_ / zoom_;

    // Calculate target position to center on point
    Vec2 targetPos(
        slidePoint.x - viewportWidth / 2.0,
        slidePoint.y - viewportHeight / 2.0
    );

    // Pre-clamp target position
    Vec2 savedPos = position_;
    position_ = targetPos;
    ClampToBounds();
    targetPos = position_;
    position_ = savedPos;

    // Start animation with specified mode
    animation_.Start(position_, zoom_, targetPos, zoom_, mode, 300.0);
}

void Viewport::ResetView(AnimationMode mode) {
    // Calculate target position at minZoom_
    double viewportWidth = windowWidth_ / minZoom_;
    double viewportHeight = windowHeight_ / minZoom_;

    Vec2 targetPos(
        (slideWidth_ - viewportWidth) / 2.0,
        (slideHeight_ - viewportHeight) / 2.0
    );

    // Pre-clamp target position
    Vec2 savedPos = position_;
    double savedZoom = zoom_;
    position_ = targetPos;
    zoom_ = minZoom_;
    ClampToBounds();
    targetPos = position_;
    double targetZoom = zoom_;
    position_ = savedPos;
    zoom_ = savedZoom;

    // Animate to reset state (longer duration for "reset" feel)
    animation_.Start(position_, zoom_, targetPos, targetZoom, mode, 500.0);
}

Vec2 Viewport::ScreenToSlide(Vec2 screenPos) const {
    // Screen coordinates → Slide coordinates
    return Vec2(
        screenPos.x / zoom_ + position_.x,
        screenPos.y / zoom_ + position_.y
    );
}

Vec2 Viewport::SlideToScreen(Vec2 slidePos) const {
    // Slide coordinates → Screen coordinates
    return Vec2(
        (slidePos.x - position_.x) * zoom_,
        (slidePos.y - position_.y) * zoom_
    );
}

Rect Viewport::GetVisibleRegion() const {
    // Calculate visible region in slide coordinates
    double viewportWidth = windowWidth_ / zoom_;
    double viewportHeight = windowHeight_ / zoom_;

    return Rect(position_.x, position_.y, viewportWidth, viewportHeight);
}

void Viewport::ClampToBounds() {
    // Calculate viewport size in slide coordinates
    double viewportWidth = windowWidth_ / zoom_;
    double viewportHeight = windowHeight_ / zoom_;

    // Allow viewport to go slightly outside bounds for better UX
    // But prevent viewport from going completely off the slide

    // Clamp X
    if (viewportWidth >= slideWidth_) {
        // Viewport wider than slide - center it
        position_.x = -(viewportWidth - slideWidth_) / 2.0;
    } else {
        // Clamp to slide bounds
        position_.x = std::clamp(position_.x, 0.0, static_cast<double>(slideWidth_ - viewportWidth));
    }

    // Clamp Y
    if (viewportHeight >= slideHeight_) {
        // Viewport taller than slide - center it
        position_.y = -(viewportHeight - slideHeight_) / 2.0;
    } else {
        // Clamp to slide bounds
        position_.y = std::clamp(position_.y, 0.0, static_cast<double>(slideHeight_ - viewportHeight));
    }
}

void Viewport::CalculateZoomLimits() {
    if (slideWidth_ == 0 || slideHeight_ == 0) {
        minZoom_ = 0.01;
        maxZoom_ = 4.0;
        return;
    }

    // Minimum zoom: entire slide fits in window with some margin
    double zoomX = static_cast<double>(windowWidth_) / slideWidth_;
    double zoomY = static_cast<double>(windowHeight_) / slideHeight_;
    minZoom_ = std::min(zoomX, zoomY) * 0.95; // 95% to add small margin

    // Maximum zoom: 4x magnification or 1:1 pixel mapping, whichever is larger
    maxZoom_ = 4.0;

    std::cout << "Viewport zoom limits: " << minZoom_ << " - " << maxZoom_ << std::endl;
}

void Viewport::UpdateAnimation(double currentTimeMs) {
    if (!animation_.IsActive()) {
        return;
    }

    // Update animation and get new position/zoom
    Vec2 newPosition;
    double newZoom;
    bool complete = animation_.Update(currentTimeMs, newPosition, newZoom);

    // Apply the animated values
    position_ = newPosition;
    zoom_ = newZoom;

    // Clamp to bounds when animation completes
    if (complete) {
        ClampToBounds();
    }
}
