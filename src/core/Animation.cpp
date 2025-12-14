#include "Animation.h"
#include "Viewport.h"  // For Vec2 definition
#include <SDL_timer.h>

// Ease-in-out cubic interpolation
double Animation::EaseInOutCubic(double t) {
    if (t < 0.5) {
        return 4.0 * t * t * t;
    } else {
        double f = (2.0 * t - 2.0);
        return 1.0 + f * f * f / 2.0;
    }
}

// Linear interpolation
double Animation::Lerp(double start, double end, double t) {
    return start + (end - start) * t;
}

// Vec2 interpolation
Vec2 Animation::LerpVec2(const Vec2& start, const Vec2& end, double t) {
    return Vec2(
        Lerp(start.x, end.x, t),
        Lerp(start.y, end.y, t)
    );
}

Animation::Animation()
    : active_(false)
    , mode_(AnimationMode::INSTANT)
    , startTime_(0.0)
    , duration_(0.0)
    , startPosition_(0.0, 0.0)
    , startZoom_(1.0)
    , targetPosition_(0.0, 0.0)
    , targetZoom_(1.0)
{
}

void Animation::Start(Vec2 startPos, double startZoom,
                     Vec2 targetPos, double targetZoom,
                     AnimationMode mode, double durationMs) {
    active_ = true;
    mode_ = mode;
    startTime_ = static_cast<double>(SDL_GetTicks());
    duration_ = durationMs;

    startPosition_ = startPos;
    startZoom_ = startZoom;
    targetPosition_ = targetPos;
    targetZoom_ = targetZoom;

    // For INSTANT mode, animation completes immediately
    // (will be handled in first Update() call)
}

bool Animation::Update(double currentTimeMs, Vec2& outPos, double& outZoom) {
    if (!active_) {
        return false;
    }

    // INSTANT mode: immediately set to target and complete
    if (mode_ == AnimationMode::INSTANT) {
        outPos = targetPosition_;
        outZoom = targetZoom_;
        active_ = false;
        return true;  // Animation complete
    }

    // SMOOTH mode: interpolate over time
    double elapsed = currentTimeMs - startTime_;
    double t = elapsed / duration_;

    if (t >= 1.0) {
        // Animation complete - snap to exact target values
        outPos = targetPosition_;
        outZoom = targetZoom_;
        active_ = false;
        return true;  // Animation complete
    }

    // Interpolate with easing
    double easedT = EaseInOutCubic(t);
    outPos = LerpVec2(startPosition_, targetPosition_, easedT);
    outZoom = Lerp(startZoom_, targetZoom_, easedT);

    return false;  // Animation still in progress
}

bool Animation::IsActive() const {
    return active_;
}

void Animation::Cancel() {
    active_ = false;
}
