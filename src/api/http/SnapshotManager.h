#pragma once

#include <string>
#include <vector>
#include <map>
#include <list>
#include <deque>
#include <mutex>
#include <chrono>
#include <optional>
#include <thread>
#include <atomic>

namespace pathview {
namespace http {

/**
 * Manages snapshot images with LRU caching
 */
class SnapshotManager {
public:
    struct Snapshot {
        std::string id;
        std::vector<uint8_t> pngData;
        int width;
        int height;
        std::chrono::steady_clock::time_point lastAccess;
    };

    explicit SnapshotManager(size_t maxSnapshots = 50);
    ~SnapshotManager();

    /**
     * Add a new snapshot and generate UUID
     * @param pngData PNG-encoded image data
     * @param width Image width
     * @param height Image height
     * @return Generated UUID for the snapshot
     */
    std::string AddSnapshot(const std::vector<uint8_t>& pngData, int width, int height);

    /**
     * Get snapshot by ID
     * Updates last access time for LRU
     * @param id Snapshot UUID
     * @return Snapshot if found, nullopt otherwise
     */
    std::optional<Snapshot> GetSnapshot(const std::string& id);

    /**
     * Remove expired snapshots (older than TTL)
     */
    void Cleanup();

    /**
     * Get number of cached snapshots
     */
    size_t GetCacheSize() const;

    /**
     * Add snapshot ID to stream buffer
     * Used for MJPEG streaming - maintains circular buffer of recent frames
     * @param id Snapshot UUID to add to stream buffer
     */
    void AddStreamFrame(const std::string& id);

    /**
     * Get the latest stream frame ID
     * @return Most recent snapshot ID in stream buffer, or empty string if none
     */
    std::string GetLatestStreamFrame() const;

private:
    void EvictOldest();
    std::string GenerateUUID();

    std::map<std::string, Snapshot> cache_;
    std::list<std::string> lruList_;  // Most recent at front
    mutable std::mutex mutex_;

    size_t maxSnapshots_;
    static constexpr std::chrono::hours TTL{1};  // 1 hour TTL

    // Stream buffer (separate from LRU cache)
    std::deque<std::string> streamFrameIds_;
    size_t maxStreamFrames_{3};

    // Cleanup thread
    std::thread cleanupThread_;
    std::atomic<bool> running_{true};
};

} // namespace http
} // namespace pathview
