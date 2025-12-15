#pragma once

#include <string>
#include <chrono>
#include "Animation.h"  // For Vec2

namespace pathview {

/**
 * Tracks the state of an in-flight animation for completion detection.
 *
 * AnimationTokens are created when viewport.move is called and allow
 * clients to poll for animation completion via viewport.await_move.
 */
struct AnimationToken {
    std::string token;                                    // UUID string
    bool completed;                                       // Animation finished?
    bool aborted;                                         // Was interrupted?
    Vec2 finalPosition;                                  // Final viewport position
    double finalZoom;                                    // Final zoom level
    std::chrono::steady_clock::time_point createdAt;     // For cleanup
};

}  // namespace pathview
