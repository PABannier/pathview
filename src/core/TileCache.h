#pragma once

#include "TextureManager.h"
#include <unordered_map>
#include <list>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <functional>
#include <atomic>

// Tile data storage
struct TileData {
    uint32_t* pixels;    // RGBA pixel data
    int32_t width;
    int32_t height;
    size_t memorySize;   // Memory usage in bytes

    TileData() : pixels(nullptr), width(0), height(0), memorySize(0) {}

    TileData(uint32_t* p, int32_t w, int32_t h)
        : pixels(p), width(w), height(h), memorySize(w * h * sizeof(uint32_t)) {}

    ~TileData() {
        if (pixels) {
            delete[] pixels;
            pixels = nullptr;
        }
    }

    // Move semantics
    TileData(TileData&& other) noexcept
        : pixels(other.pixels), width(other.width), height(other.height), memorySize(other.memorySize) {
        other.pixels = nullptr;
    }

    TileData& operator=(TileData&& other) noexcept {
        if (this != &other) {
            if (pixels) delete[] pixels;
            pixels = other.pixels;
            width = other.width;
            height = other.height;
            memorySize = other.memorySize;
            other.pixels = nullptr;
        }
        return *this;
    }

    // Delete copy
    TileData(const TileData&) = delete;
    TileData& operator=(const TileData&) = delete;
};

// Cache entry with LRU metadata
struct CacheEntry {
    TileData data;
    std::list<TileKey>::iterator lruIterator;

    CacheEntry(TileData&& d, std::list<TileKey>::iterator it)
        : data(std::move(d)), lruIterator(it) {}
};

class TileCache {
public:
    explicit TileCache(size_t maxMemoryBytes = 512 * 1024 * 1024); // Default: 512MB
    ~TileCache();

    // Get tile data (returns nullptr if not in cache)
    const TileData* GetTile(const TileKey& key);

    // Insert tile data (takes ownership of pixels)
    void InsertTile(const TileKey& key, TileData&& data);

    // Check if tile is cached
    bool HasTile(const TileKey& key) const;

    // Clear all cached tiles
    void Clear();

    // Cache statistics
    size_t GetTileCount() const { return cache_.size(); }
    size_t GetMemoryUsage() const { return currentMemoryUsage_; }
    size_t GetMaxMemory() const { return maxMemoryBytes_; }
    size_t GetHitCount() const { return hitCount_; }
    size_t GetMissCount() const { return missCount_; }
    double GetHitRate() const {
        size_t total = hitCount_ + missCount_;
        return total > 0 ? static_cast<double>(hitCount_) / total : 0.0;
    }

private:
    void EvictLRU();
    void TouchTile(const TileKey& key);

    mutable std::shared_mutex cacheMutex_;  // Thread safety: read/write lock

    std::unordered_map<TileKey, CacheEntry, TileKeyHash> cache_;
    std::list<TileKey> lruList_;  // Front = most recent, back = least recent

    size_t maxMemoryBytes_;
    size_t currentMemoryUsage_;

    // Statistics (atomic for thread-safe reads)
    std::atomic<size_t> hitCount_;
    std::atomic<size_t> missCount_;
};
