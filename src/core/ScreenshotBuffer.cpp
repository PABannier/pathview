#include "ScreenshotBuffer.h"

namespace pathview {

ScreenshotBuffer::ScreenshotBuffer()
    : width_(0)
    , height_(0)
    , ready_(false)
    , captureRequested_(false)
{
}

void ScreenshotBuffer::RequestCapture() {
    captureRequested_ = true;
}

bool ScreenshotBuffer::IsCaptureRequested() const {
    return captureRequested_;
}

void ScreenshotBuffer::ClearCaptureRequest() {
    captureRequested_ = false;
}

bool ScreenshotBuffer::IsReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

void ScreenshotBuffer::StoreCapture(const std::vector<uint8_t>& pixels, int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    pixels_ = pixels;
    width_ = width;
    height_ = height;
    ready_ = true;
}

bool ScreenshotBuffer::GetCapture(std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ready_) {
        return false;
    }

    outPixels = pixels_;
    outWidth = width_;
    outHeight = height_;
    return true;
}

void ScreenshotBuffer::MarkAsRead() {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = false;
}

} // namespace pathview
