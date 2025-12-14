#pragma once

// Vec2 definition (moved from Viewport.h to avoid circular dependency)
struct Vec2 {
    double x;
    double y;

    Vec2() : x(0.0), y(0.0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
    Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
};

enum class AnimationMode {
    INSTANT,    // Immediate update, no interpolation (manual input)
    SMOOTH      // Ease-in/ease-out cubic interpolation (MCP control)
};

class Animation {
public:
    Animation();

    // Start a new animation
    void Start(Vec2 startPos, double startZoom,
               Vec2 targetPos, double targetZoom,
               AnimationMode mode, double durationMs = 300.0);

    // Update animation state (called each frame)
    // Returns true if animation is complete
    bool Update(double currentTimeMs, Vec2& outPos, double& outZoom);

    // Check if animation is active
    bool IsActive() const;

    // Cancel current animation
    void Cancel();

private:
    bool active_;
    AnimationMode mode_;
    double startTime_;
    double duration_;

    Vec2 startPosition_;
    double startZoom_;
    Vec2 targetPosition_;
    double targetZoom_;

    // Easing and interpolation functions for SMOOTH mode
    static double EaseInOutCubic(double t);
    static double Lerp(double start, double end, double t);
    static Vec2 LerpVec2(const Vec2& start, const Vec2& end, double t);
};
