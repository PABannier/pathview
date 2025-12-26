#pragma once

#include "TileLoadRequest.h"
#include "TileCache.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <atomic>
#include <set>

class SlideLoader;

class TileLoadThreadPool {
public:
    using TileReadyCallback = std::function<void(const TileKey&)>;

    explicit TileLoadThreadPool(size_t numThreads = 4);
    ~TileLoadThreadPool();

    // Initialize with dependencies (must be called before Start)
    void Initialize(SlideLoader* loader, TileCache* cache, TileReadyCallback onTileReady);

    // Start/stop the thread pool
    void Start();
    void Stop();

    // Submit a tile load request
    void SubmitRequest(const TileLoadRequest& request);

    // Cancel a specific request (if not yet started)
    void CancelRequest(const TileKey& key);

    // Cancel all pending requests
    void CancelAllRequests();

    // Check if a tile is currently pending (in queue or being processed)
    bool IsPending(const TileKey& key) const;

    // Statistics
    size_t GetPendingCount() const;
    size_t GetActiveCount() const { return activeCount_.load(); }

private:
    void WorkerLoop();
    bool PopNextRequest(TileLoadRequest& outRequest);
    void ProcessRequest(const TileLoadRequest& request);

    // Dependencies
    SlideLoader* loader_ = nullptr;
    TileCache* cache_ = nullptr;
    TileReadyCallback onTileReady_;

    // Thread pool
    std::vector<std::thread> workers_;
    size_t numThreads_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> activeCount_{0};

    // Request queue (priority queue: highest priority first)
    std::priority_queue<TileLoadRequest> requestQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Track pending tiles to avoid duplicate requests
    std::set<TileKey> pendingKeys_;
    mutable std::mutex pendingMutex_;
};
