#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace pathview {

/**
 * ScreenshotBuffer - Thread-safe buffer for screenshot capture
 *
 * This class manages a buffer for capturing screenshots from the renderer.
 * It provides thread-safe access for the rendering thread to write pixel data
 * and for the IPC handler thread to read and encode the captured data.
 */
class ScreenshotBuffer {
public:
    ScreenshotBuffer();
    ~ScreenshotBuffer() = default;

    // Delete copy/move to ensure single ownership
    ScreenshotBuffer(const ScreenshotBuffer&) = delete;
    ScreenshotBuffer& operator=(const ScreenshotBuffer&) = delete;
    ScreenshotBuffer(ScreenshotBuffer&&) = delete;
    ScreenshotBuffer& operator=(ScreenshotBuffer&&) = delete;

    /**
     * Request a screenshot capture on the next render frame
     */
    void RequestCapture();

    /**
     * Check if a capture is currently requested
     */
    bool IsCaptureRequested() const;

    /**
     * Clear the capture request flag
     */
    void ClearCaptureRequest();

    /**
     * Check if captured data is ready to read
     */
    bool IsReady() const;

    /**
     * Store captured pixel data (called by rendering thread)
     * @param pixels RGBA pixel data
     * @param width Image width
     * @param height Image height
     */
    void StoreCapture(const std::vector<uint8_t>& pixels, int width, int height);

    /**
     * Get captured pixel data (thread-safe)
     * @param outPixels Output vector for pixel data
     * @param outWidth Output width
     * @param outHeight Output height
     * @return true if data was available, false otherwise
     */
    bool GetCapture(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight);

    /**
     * Mark the buffer as read (clears ready flag)
     */
    void MarkAsRead();

private:
    std::vector<uint8_t> pixels_;
    int width_;
    int height_;
    bool ready_;
    mutable std::mutex mutex_;  // Mutable to allow locking in const methods
    std::atomic<bool> captureRequested_;
};

} // namespace pathview
