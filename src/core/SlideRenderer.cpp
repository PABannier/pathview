#include "SlideRenderer.h"
#include "SlideLoader.h"
#include "Viewport.h"
#include "TextureManager.h"
#include "TileCache.h"
#include "TileLoadThreadPool.h"
#include "TileLoadRequest.h"
#include <iostream>
#include <cmath>
#include <algorithm>

static const int32_t NUM_WORKER_THREADS = 4;

SlideRenderer::SlideRenderer(SlideLoader* loader, SDL_Renderer* renderer, TextureManager* textureManager)
    : loader_(loader)
    , renderer_(renderer)
    , textureManager_(textureManager)
    , tileCache_(std::make_unique<TileCache>())
{
}

SlideRenderer::~SlideRenderer() {
    Shutdown();
}

void SlideRenderer::Initialize() {
    if (!threadPool_) {
        threadPool_ = std::make_unique<TileLoadThreadPool>(NUM_WORKER_THREADS);
        threadPool_->Initialize(loader_, tileCache_.get(),
            [this](const TileKey& key) { OnTileReady(key); });
        threadPool_->Start();
        std::cout << "SlideRenderer: Async tile loading initialized" << std::endl;
    }
}

void SlideRenderer::Shutdown() {
    if (threadPool_) {
        threadPool_->Stop();
        threadPool_.reset();
        std::cout << "SlideRenderer: Async tile loading shutdown" << std::endl;
    }
}

void SlideRenderer::Render(const Viewport& viewport) {
    if (!loader_ || !loader_->IsValid()) {
        return;
    }

    // Select appropriate level based on zoom
    int32_t level = SelectLevel(viewport.GetZoom());

    // Render using tiles
    RenderTiled(viewport, level);
}

size_t SlideRenderer::GetCacheTileCount() const {
    return tileCache_ ? tileCache_->GetTileCount() : 0;
}

size_t SlideRenderer::GetCacheMemoryUsage() const {
    return tileCache_ ? tileCache_->GetMemoryUsage() : 0;
}

double SlideRenderer::GetCacheHitRate() const {
    return tileCache_ ? tileCache_->GetHitRate() : 0.0;
}

size_t SlideRenderer::GetPendingTileCount() const {
    return threadPool_ ? threadPool_->GetPendingCount() : 0;
}

void SlideRenderer::OnTileReady(const TileKey& key) {
    // Called from background thread when a tile finishes loading
    // Remove from our pending set
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingTiles_.erase(key);
    // The tile is now in cache and will be picked up on next render frame
}

int32_t SlideRenderer::SelectLevel(double zoom) const {
    // Goal: Select level where downsample ≈ 1/zoom
    // At 100% zoom (1.0), we want level 0 (downsample 1)
    // At 50% zoom (0.5), we want a level with downsample ~2
    // At 25% zoom (0.25), we want a level with downsample ~4

    double targetDownsample = 1.0 / zoom;
    int32_t levelCount = loader_->GetLevelCount();

    int32_t bestLevel = 0;
    double bestDiff = std::abs(loader_->GetLevelDownsample(0) - targetDownsample);

    for (int32_t i = 1; i < levelCount; ++i) {
        double downsample = loader_->GetLevelDownsample(i);
        double diff = std::abs(downsample - targetDownsample);

        // Prefer higher resolution when between two levels to avoid pixelation
        if (diff < bestDiff || (diff == bestDiff && downsample < loader_->GetLevelDownsample(bestLevel))) {
            bestDiff = diff;
            bestLevel = i;
        }
    }

    return bestLevel;
}

void SlideRenderer::RenderTiled(const Viewport& viewport, int32_t level) {
    // Enumerate visible tiles
    std::vector<TileKey> visibleTiles = EnumerateVisibleTiles(viewport, level);

    // Load and render each visible tile
    for (const auto& tileKey : visibleTiles) {
        LoadAndRenderTile(tileKey, viewport, level);
    }
}

std::vector<TileKey> SlideRenderer::EnumerateVisibleTiles(const Viewport& viewport, int32_t level) const {
    std::vector<TileKey> tiles;

    // Get visible region in slide coordinates (level 0)
    Rect visibleRegion = viewport.GetVisibleRegion();

    // Get downsample factor for this level
    double downsample = loader_->GetLevelDownsample(level);

    // Convert visible region to level coordinates
    int64_t levelLeft = static_cast<int64_t>(visibleRegion.x / downsample);
    int64_t levelTop = static_cast<int64_t>(visibleRegion.y / downsample);
    int64_t levelRight = static_cast<int64_t>((visibleRegion.x + visibleRegion.width) / downsample);
    int64_t levelBottom = static_cast<int64_t>((visibleRegion.y + visibleRegion.height) / downsample);

    // Get level dimensions
    auto levelDims = loader_->GetLevelDimensions(level);

    // Clamp to level bounds
    levelLeft = std::max(0LL, levelLeft);
    levelTop = std::max(0LL, levelTop);
    levelRight = std::min(levelDims.width, levelRight);
    levelBottom = std::min(levelDims.height, levelBottom);

    // Calculate tile indices
    int32_t startTileX = static_cast<int32_t>(levelLeft / TILE_SIZE);
    int32_t startTileY = static_cast<int32_t>(levelTop / TILE_SIZE);
    int32_t endTileX = static_cast<int32_t>(levelRight / TILE_SIZE);
    int32_t endTileY = static_cast<int32_t>(levelBottom / TILE_SIZE);

    // Enumerate all visible tiles
    for (int32_t ty = startTileY; ty <= endTileY; ++ty) {
        for (int32_t tx = startTileX; tx <= endTileX; ++tx) {
            tiles.push_back({level, tx, ty});
        }
    }

    return tiles;
}

void SlideRenderer::LoadAndRenderTile(const TileKey& key, const Viewport& viewport, int32_t level) {
    // 1. Check cache - if hit, render immediately
    const TileData* cachedTile = tileCache_->GetTile(key);
    if (cachedTile) {
        RenderTileToScreen(key, cachedTile, viewport, level);
        return;
    }

    // 2. Cache miss - find and render fallback from coarser pyramid level
    TileKey fallbackKey;
    const TileData* fallbackTile = FindBestFallback(key, &fallbackKey);
    if (fallbackTile) {
        RenderFallbackTile(key, fallbackKey, fallbackTile, viewport, level);
    }

    // 3. Submit async load request if thread pool is available and tile not already pending
    if (threadPool_) {
        bool alreadyPending = false;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            alreadyPending = pendingTiles_.find(key) != pendingTiles_.end();
            if (!alreadyPending) {
                pendingTiles_.insert(key);
            }
        }

        if (!alreadyPending) {
            TileLoadPriority priority = fallbackTile
                ? TileLoadPriority::VISIBLE    // Has fallback showing
                : TileLoadPriority::URGENT;    // No fallback, high priority
            threadPool_->SubmitRequest(TileLoadRequest(key, priority));
        }
    }
}

const TileData* SlideRenderer::FindBestFallback(const TileKey& key, TileKey* outFallbackKey) {
    // Search from next coarser level down to lowest resolution
    int32_t levelCount = loader_->GetLevelCount();
    double targetDownsample = loader_->GetLevelDownsample(key.level);

    for (int32_t l = key.level + 1; l < levelCount; ++l) {
        double fallbackDownsample = loader_->GetLevelDownsample(l);
        double ratio = fallbackDownsample / targetDownsample;

        // Calculate equivalent tile position at coarser level
        int32_t fallbackTileX = static_cast<int32_t>(key.tileX / ratio);
        int32_t fallbackTileY = static_cast<int32_t>(key.tileY / ratio);

        TileKey fallbackKey{l, fallbackTileX, fallbackTileY};

        const TileData* tile = tileCache_->GetTile(fallbackKey);
        if (tile) {
            *outFallbackKey = fallbackKey;
            return tile;
        }
    }

    return nullptr;  // No fallback available
}

void SlideRenderer::RenderFallbackTile(const TileKey& targetKey, const TileKey& fallbackKey,
                                        const TileData* fallbackTile, const Viewport& viewport,
                                        int32_t targetLevel) {
    // We have a coarser tile and need to render a portion of it scaled up
    // to cover where the target high-res tile would be

    double targetDownsample = loader_->GetLevelDownsample(targetLevel);
    double fallbackDownsample = loader_->GetLevelDownsample(fallbackKey.level);

    // Calculate the target tile's position in slide coordinates (level 0)
    double targetX0 = targetKey.tileX * TILE_SIZE * targetDownsample;
    double targetY0 = targetKey.tileY * TILE_SIZE * targetDownsample;
    double targetX1 = targetX0 + TILE_SIZE * targetDownsample;
    double targetY1 = targetY0 + TILE_SIZE * targetDownsample;

    // Calculate the fallback tile's position in slide coordinates (level 0)
    double fallbackX0 = fallbackKey.tileX * TILE_SIZE * fallbackDownsample;
    double fallbackY0 = fallbackKey.tileY * TILE_SIZE * fallbackDownsample;

    // Calculate source rect within the fallback tile (in fallback tile pixel coords)
    double srcX0 = (targetX0 - fallbackX0) / fallbackDownsample;
    double srcY0 = (targetY0 - fallbackY0) / fallbackDownsample;
    double srcX1 = (targetX1 - fallbackX0) / fallbackDownsample;
    double srcY1 = (targetY1 - fallbackY0) / fallbackDownsample;

    // Clamp source rect to fallback tile bounds
    srcX0 = std::max(0.0, std::min(srcX0, static_cast<double>(fallbackTile->width)));
    srcY0 = std::max(0.0, std::min(srcY0, static_cast<double>(fallbackTile->height)));
    srcX1 = std::max(0.0, std::min(srcX1, static_cast<double>(fallbackTile->width)));
    srcY1 = std::max(0.0, std::min(srcY1, static_cast<double>(fallbackTile->height)));

    if (srcX1 <= srcX0 || srcY1 <= srcY0) {
        return;  // No valid source region
    }

    SDL_Rect srcRect = {
        static_cast<int>(srcX0),
        static_cast<int>(srcY0),
        static_cast<int>(srcX1 - srcX0),
        static_cast<int>(srcY1 - srcY0)
    };

    // Get or create texture for the fallback tile
    SDL_Texture* texture = textureManager_->GetOrCreateTexture(
        fallbackKey,
        fallbackTile->pixels,
        fallbackTile->width,
        fallbackTile->height
    );

    if (!texture) {
        return;
    }

    // Calculate destination rect in screen coordinates
    Vec2 topLeft = viewport.SlideToScreen(Vec2(targetX0, targetY0));
    Vec2 bottomRight = viewport.SlideToScreen(Vec2(targetX1, targetY1));

    SDL_Rect dstRect = {
        static_cast<int>(topLeft.x),
        static_cast<int>(topLeft.y),
        static_cast<int>(bottomRight.x - topLeft.x),
        static_cast<int>(bottomRight.y - topLeft.y)
    };

    // Render the fallback tile portion scaled up
    SDL_RenderCopy(renderer_, texture, &srcRect, &dstRect);
}

void SlideRenderer::RenderTileToScreen(const TileKey& key, const TileData* tileData,
                                        const Viewport& viewport, int32_t level) {
    // Create or get SDL texture
    SDL_Texture* texture = textureManager_->GetOrCreateTexture(
        key,
        tileData->pixels,
        tileData->width,
        tileData->height
    );

    if (!texture) {
        return;
    }

    // Calculate tile position in slide coordinates (level 0)
    double downsample = loader_->GetLevelDownsample(level);
    double tileX0 = key.tileX * TILE_SIZE * downsample;
    double tileY0 = key.tileY * TILE_SIZE * downsample;
    double tileX1 = tileX0 + tileData->width * downsample;
    double tileY1 = tileY0 + tileData->height * downsample;

    // Convert to screen coordinates
    Vec2 topLeft = viewport.SlideToScreen(Vec2(tileX0, tileY0));
    Vec2 bottomRight = viewport.SlideToScreen(Vec2(tileX1, tileY1));

    SDL_Rect dstRect = {
        static_cast<int>(topLeft.x),
        static_cast<int>(topLeft.y),
        static_cast<int>(bottomRight.x - topLeft.x),
        static_cast<int>(bottomRight.y - topLeft.y)
    };

    // Render tile
    SDL_RenderCopy(renderer_, texture, nullptr, &dstRect);
}
