#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include <unordered_map>
#include <string>

struct TileKey {
    int32_t level;
    int32_t tileX;
    int32_t tileY;

    bool operator==(const TileKey& other) const {
        return level == other.level && tileX == other.tileX && tileY == other.tileY;
    }

    bool operator<(const TileKey& other) const {
        if (level != other.level) return level < other.level;
        if (tileX != other.tileX) return tileX < other.tileX;
        return tileY < other.tileY;
    }

    std::string ToString() const;
};

// Hash function for TileKey
struct TileKeyHash {
    size_t operator()(const TileKey& k) const {
        return ((size_t)k.level << 48) | ((size_t)k.tileX << 24) | (size_t)k.tileY;
    }
};

class TextureManager {
public:
    explicit TextureManager(SDL_Renderer* renderer);
    ~TextureManager();

    // Delete copy, allow move
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Create texture from RGBA pixel data
    SDL_Texture* CreateTexture(const uint32_t* pixels, int32_t width, int32_t height);

    // Get or create texture for a tile
    SDL_Texture* GetOrCreateTexture(const TileKey& key, const uint32_t* pixels, int32_t width, int32_t height);

    // Destroy a specific texture
    void DestroyTexture(SDL_Texture* texture);

    // Clear all cached textures
    void ClearCache();

    // Get cache statistics
    size_t GetCacheSize() const { return textureCache_.size(); }

private:
    SDL_Renderer* renderer_;
    std::unordered_map<TileKey, SDL_Texture*, TileKeyHash> textureCache_;
};
