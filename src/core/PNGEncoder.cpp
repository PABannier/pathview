#include "PNGEncoder.h"
#include <png.h>
#include <cstring>
#include <stdexcept>

namespace pathview {

// libpng write callback - writes PNG data to std::vector
static void PNGWriteCallback(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto* vec = reinterpret_cast<std::vector<uint8_t>*>(png_get_io_ptr(png_ptr));
    vec->insert(vec->end(), data, data + length);
}

std::vector<uint8_t> PNGEncoder::Encode(const std::vector<uint8_t>& pixels,
                                        int width, int height) {
    // Validate input
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid image dimensions");
    }

    size_t expectedSize = static_cast<size_t>(width) * height * 4;
    if (pixels.size() != expectedSize) {
        throw std::runtime_error("Pixel data size mismatch");
    }

    std::vector<uint8_t> pngData;
    pngData.reserve(expectedSize / 2);  // Estimate compressed size

    // Create libpng write structure
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                               nullptr, nullptr, nullptr);
    if (!png) {
        throw std::runtime_error("Failed to create PNG write struct");
    }

    // Create libpng info structure
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        throw std::runtime_error("Failed to create PNG info struct");
    }

    // Set up error handling (libpng uses longjmp)
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        throw std::runtime_error("PNG encoding error");
    }

    // Set custom write function to write to std::vector
    png_set_write_fn(png, &pngData, PNGWriteCallback, nullptr);

    // Set image parameters
    // RGBA format, 8 bits per channel, no interlacing
    png_set_IHDR(png, info, width, height, 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    // Write PNG header
    png_write_info(png, info);

    // Write image data row by row
    std::vector<png_bytep> rows(height);
    for (int y = 0; y < height; y++) {
        rows[y] = const_cast<png_bytep>(&pixels[y * width * 4]);
    }
    png_write_image(png, rows.data());

    // Finish writing
    png_write_end(png, nullptr);

    // Cleanup
    png_destroy_write_struct(&png, &info);

    return pngData;
}

} // namespace pathview
