#include "TileLoadThreadPool.h"
#include "SlideLoader.h"
#include <iostream>

TileLoadThreadPool::TileLoadThreadPool(size_t numThreads)
    : numThreads_(numThreads)
{
}

TileLoadThreadPool::~TileLoadThreadPool() {
    Stop();
}

void TileLoadThreadPool::Initialize(SlideLoader* loader, TileCache* cache, TileReadyCallback onTileReady) {
    loader_ = loader;
    cache_ = cache;
    onTileReady_ = std::move(onTileReady);
}

void TileLoadThreadPool::Start() {
    if (running_.load()) {
        return;  // Already running
    }

    if (!loader_ || !cache_) {
        std::cerr << "TileLoadThreadPool: Cannot start without loader and cache" << std::endl;
        return;
    }

    running_.store(true);

    // Create worker threads
    workers_.reserve(numThreads_);
    for (size_t i = 0; i < numThreads_; ++i) {
        workers_.emplace_back(&TileLoadThreadPool::WorkerLoop, this);
    }

    std::cout << "TileLoadThreadPool: Started " << numThreads_ << " worker threads" << std::endl;
}

void TileLoadThreadPool::Stop() {
    if (!running_.load()) {
        return;  // Not running
    }

    // Signal threads to stop
    running_.store(false);

    // Wake up all waiting threads
    queueCondition_.notify_all();

    // Join all threads
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Clear pending requests
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!requestQueue_.empty()) {
            requestQueue_.pop();
        }
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingKeys_.clear();
    }

    std::cout << "TileLoadThreadPool: Stopped" << std::endl;
}

void TileLoadThreadPool::SubmitRequest(const TileLoadRequest& request) {
    // Check if already pending
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (pendingKeys_.find(request.key) != pendingKeys_.end()) {
            return;  // Already in queue
        }
        pendingKeys_.insert(request.key);
    }

    // Check if already in cache
    if (cache_ && cache_->HasTile(request.key)) {
        // Already cached, remove from pending
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingKeys_.erase(request.key);
        return;
    }

    // Add to queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        requestQueue_.push(request);
    }

    // Wake up a worker
    queueCondition_.notify_one();
}

void TileLoadThreadPool::CancelRequest(const TileKey& key) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingKeys_.erase(key);
    // Note: Request may still be in queue, but will be skipped when processed
}

void TileLoadThreadPool::CancelAllRequests() {
    // Clear the queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!requestQueue_.empty()) {
            requestQueue_.pop();
        }
    }

    // Clear pending set
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingKeys_.clear();
    }
}

bool TileLoadThreadPool::IsPending(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    return pendingKeys_.find(key) != pendingKeys_.end();
}

size_t TileLoadThreadPool::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return requestQueue_.size();
}

void TileLoadThreadPool::WorkerLoop() {
    while (running_.load()) {
        TileLoadRequest request;

        if (!PopNextRequest(request)) {
            continue;  // No work or shutting down
        }

        // Check if request was cancelled
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            if (pendingKeys_.find(request.key) == pendingKeys_.end()) {
                continue;  // Cancelled
            }
        }

        // Process the request
        activeCount_++;
        ProcessRequest(request);
        activeCount_--;

        // Remove from pending set
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pendingKeys_.erase(request.key);
        }
    }
}

bool TileLoadThreadPool::PopNextRequest(TileLoadRequest& outRequest) {
    std::unique_lock<std::mutex> lock(queueMutex_);

    // Wait for work or shutdown
    queueCondition_.wait(lock, [this]() {
        return !requestQueue_.empty() || !running_.load();
    });

    if (!running_.load() && requestQueue_.empty()) {
        return false;  // Shutting down
    }

    if (requestQueue_.empty()) {
        return false;
    }

    outRequest = requestQueue_.top();
    requestQueue_.pop();
    return true;
}

void TileLoadThreadPool::ProcessRequest(const TileLoadRequest& request) {
    if (!loader_ || !cache_) {
        return;
    }

    // Check if already cached (might have been loaded by another thread)
    if (cache_->HasTile(request.key)) {
        if (onTileReady_) {
            onTileReady_(request.key);
        }
        return;
    }

    // Load tile from slide
    const TileKey& key = request.key;
    double downsample = loader_->GetLevelDownsample(key.level);

    // Calculate tile position in level 0 coordinates
    int64_t x0 = static_cast<int64_t>(key.tileX * 512 * downsample);  // TILE_SIZE = 512
    int64_t y0 = static_cast<int64_t>(key.tileY * 512 * downsample);

    // Calculate tile dimensions at this level
    auto levelDims = loader_->GetLevelDimensions(key.level);
    int64_t levelX = key.tileX * 512;
    int64_t levelY = key.tileY * 512;

    int64_t tileWidth = std::min(static_cast<int64_t>(512), levelDims.width - levelX);
    int64_t tileHeight = std::min(static_cast<int64_t>(512), levelDims.height - levelY);

    if (tileWidth <= 0 || tileHeight <= 0) {
        return;
    }

    // Read tile from slide (this is the blocking I/O that we moved off the render thread!)
    uint32_t* pixels = loader_->ReadRegion(key.level, x0, y0, tileWidth, tileHeight);
    if (!pixels) {
        return;
    }

    // Store in cache
    TileData tileData(pixels, tileWidth, tileHeight);
    cache_->InsertTile(key, std::move(tileData));

    // Notify that tile is ready
    if (onTileReady_) {
        onTileReady_(key);
    }
}
