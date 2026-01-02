#include "TileCache.h"
#include <iostream>
#include <mutex>

TileCache::TileCache(size_t maxMemoryBytes)
    : maxMemoryBytes_(maxMemoryBytes)
    , currentMemoryUsage_(0)
    , hitCount_(0)
    , missCount_(0)
{
    std::cout << "TileCache initialized with " << (maxMemoryBytes_ / (1024 * 1024))
              << "MB max memory" << std::endl;
}

TileCache::~TileCache() {
    Clear();

    std::cout << "TileCache statistics:" << std::endl;
    std::cout << "  Total requests: " << (hitCount_ + missCount_) << std::endl;
    std::cout << "  Cache hits: " << hitCount_ << std::endl;
    std::cout << "  Cache misses: " << missCount_ << std::endl;
    std::cout << "  Hit rate: " << (GetHitRate() * 100.0) << "%" << std::endl;
}

const TileData* TileCache::GetTile(const TileKey& key) {
    // Use unique_lock because TouchTile modifies LRU list on hit
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    auto it = cache_.find(key);

    if (it != cache_.end()) {
        // Cache hit
        hitCount_++;
        TouchTile(key);
        return &it->second.data;
    }

    // Cache miss
    missCount_++;
    return nullptr;
}

void TileCache::InsertTile(const TileKey& key, TileData&& data) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    // Check if already in cache
    if (cache_.find(key) != cache_.end()) {
        // Already cached, just touch it
        TouchTile(key);
        return;
    }

    size_t tileMemory = data.memorySize;

    // Evict LRU tiles if necessary to make room
    while (currentMemoryUsage_ + tileMemory > maxMemoryBytes_ && !lruList_.empty()) {
        EvictLRU();
    }

    // Add to front of LRU list (most recent)
    lruList_.push_front(key);
    auto lruIt = lruList_.begin();

    // Insert into cache
    cache_.emplace(key, CacheEntry(std::move(data), lruIt));

    currentMemoryUsage_ += tileMemory;
}

bool TileCache::HasTile(const TileKey& key) const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    return cache_.find(key) != cache_.end();
}

void TileCache::Clear() {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    cache_.clear();
    lruList_.clear();
    currentMemoryUsage_ = 0;
}

void TileCache::EvictLRU() {
    if (lruList_.empty()) {
        return;
    }

    // Get least recently used tile (back of list)
    TileKey lruKey = lruList_.back();
    lruList_.pop_back();

    // Find and remove from cache
    auto it = cache_.find(lruKey);
    if (it != cache_.end()) {
        currentMemoryUsage_ -= it->second.data.memorySize;
        cache_.erase(it);
    }
}

void TileCache::TouchTile(const TileKey& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return;
    }

    // Move to front of LRU list (most recent)
    lruList_.splice(lruList_.begin(), lruList_, it->second.lruIterator);
}
