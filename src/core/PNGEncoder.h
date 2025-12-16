#pragma once

#include <vector>
#include <cstdint>

namespace pathview {

/**
 * PNGEncoder - Encodes RGBA pixel data to PNG format
 *
 * This class provides a simple interface to encode raw RGBA pixel data
 * into PNG format using libpng. It's designed for screenshot capture
 * and streaming functionality.
 */
class PNGEncoder {
public:
    /**
     * Encode RGBA pixels to PNG format
     *
     * @param pixels Vector containing RGBA pixel data (4 bytes per pixel)
     *               Format: R, G, B, A (0-255 each)
     *               Row-major order (top to bottom, left to right)
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @return Vector containing PNG-encoded data
     * @throws std::runtime_error if encoding fails
     */
    static std::vector<uint8_t> Encode(const std::vector<uint8_t>& pixels,
                                       int width, int height);
};

} // namespace pathview
