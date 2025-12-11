#include "SnapshotManager.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace pathview {
namespace http {

SnapshotManager::SnapshotManager(size_t maxSnapshots)
    : maxSnapshots_(maxSnapshots)
{
}

std::string SnapshotManager::AddSnapshot(const std::vector<uint8_t>& pngData, int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Generate UUID
    std::string id = GenerateUUID();

    // Evict oldest if cache is full
    while (cache_.size() >= maxSnapshots_) {
        EvictOldest();
    }

    // Create snapshot
    Snapshot snapshot{
        id,
        pngData,
        width,
        height,
        std::chrono::steady_clock::now()
    };

    // Add to cache and LRU list
    cache_[id] = std::move(snapshot);
    lruList_.push_front(id);

    return id;
}

std::optional<SnapshotManager::Snapshot> SnapshotManager::GetSnapshot(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(id);
    if (it == cache_.end()) {
        return std::nullopt;
    }

    // Update last access time
    it->second.lastAccess = std::chrono::steady_clock::now();

    // Move to front of LRU list
    lruList_.remove(id);
    lruList_.push_front(id);

    return it->second;
}

void SnapshotManager::Cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();

    // Remove expired snapshots
    auto it = cache_.begin();
    while (it != cache_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::hours>(now - it->second.lastAccess);
        if (age >= TTL) {
            lruList_.remove(it->first);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t SnapshotManager::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

void SnapshotManager::EvictOldest() {
    if (lruList_.empty()) {
        return;
    }

    // Remove oldest (back of list)
    std::string oldestId = lruList_.back();
    lruList_.pop_back();
    cache_.erase(oldestId);
}

std::string SnapshotManager::GenerateUUID() {
    // Simple UUID v4 generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    // Generate 16 random bytes
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }

        int byte = dis(gen);

        // Set version (4) and variant bits
        if (i == 6) {
            byte = (byte & 0x0F) | 0x40;  // Version 4
        } else if (i == 8) {
            byte = (byte & 0x3F) | 0x80;  // Variant
        }

        oss << std::setw(2) << byte;
    }

    return oss.str();
}

} // namespace http
} // namespace pathview
