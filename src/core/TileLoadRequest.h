#pragma once

#include "TextureManager.h"  // For TileKey
#include <chrono>

// Priority levels for tile loading
enum class TileLoadPriority : int32_t {
    URGENT = 1000,    // Currently visible, no fallback available
    VISIBLE = 500,    // Currently visible, has fallback showing
    ADJACENT = 100    // Adjacent to viewport (for future prefetch)
};

// Request for loading a tile in background
struct TileLoadRequest {
    TileKey key;
    TileLoadPriority priority;
    std::chrono::steady_clock::time_point requestTime;

    TileLoadRequest()
        : key{0, 0, 0}
        , priority(TileLoadPriority::VISIBLE)
        , requestTime(std::chrono::steady_clock::now())
    {}

    TileLoadRequest(const TileKey& k, TileLoadPriority p)
        : key(k)
        , priority(p)
        , requestTime(std::chrono::steady_clock::now())
    {}

    // For priority queue: higher priority values should come first
    bool operator<(const TileLoadRequest& other) const {
        // Lower priority value = lower in queue (processed later)
        // Higher priority value = higher in queue (processed first)
        if (static_cast<int32_t>(priority) != static_cast<int32_t>(other.priority)) {
            return static_cast<int32_t>(priority) < static_cast<int32_t>(other.priority);
        }
        // Same priority: older requests first (FIFO within priority)
        return requestTime > other.requestTime;
    }
};
