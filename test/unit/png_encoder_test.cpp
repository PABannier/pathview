#include <gtest/gtest.h>
#include "../../src/core/PNGEncoder.h"
#include <vector>
#include <cstdint>

using namespace pathview;

// Test encoding a simple small image
TEST(PNGEncoderTest, EncodeSimpleImage) {
    // Create 10x10 red image (RGBA format)
    const int width = 10;
    const int height = 10;
    std::vector<uint8_t> pixels(width * height * 4);

    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 255;     // R
        pixels[i + 1] = 0;   // G
        pixels[i + 2] = 0;   // B
        pixels[i + 3] = 255; // A
    }

    auto png = PNGEncoder::Encode(pixels, width, height);

    // Verify PNG data was generated
    EXPECT_GT(png.size(), 0);

    // Verify PNG header (magic bytes)
    ASSERT_GE(png.size(), 8);
    EXPECT_EQ(png[0], 0x89);
    EXPECT_EQ(png[1], 0x50); // 'P'
    EXPECT_EQ(png[2], 0x4E); // 'N'
    EXPECT_EQ(png[3], 0x47); // 'G'
    EXPECT_EQ(png[4], 0x0D);
    EXPECT_EQ(png[5], 0x0A);
    EXPECT_EQ(png[6], 0x1A);
    EXPECT_EQ(png[7], 0x0A);
}

// Test encoding a larger image
TEST(PNGEncoderTest, EncodeLargeImage) {
    const int width = 1920;
    const int height = 1080;
    std::vector<uint8_t> pixels(width * height * 4);

    // Fill with gradient pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            pixels[idx] = static_cast<uint8_t>(x % 256);     // R
            pixels[idx + 1] = static_cast<uint8_t>(y % 256); // G
            pixels[idx + 2] = 128;                           // B
            pixels[idx + 3] = 255;                           // A
        }
    }

    auto png = PNGEncoder::Encode(pixels, width, height);

    // Verify PNG was created
    EXPECT_GT(png.size(), 0);

    // Verify compression worked (PNG should be smaller than raw pixels)
    EXPECT_LT(png.size(), pixels.size());

    // Verify PNG header
    ASSERT_GE(png.size(), 8);
    EXPECT_EQ(png[0], 0x89);
    EXPECT_EQ(png[1], 0x50);
}

// Test encoding a solid color image
TEST(PNGEncoderTest, EncodeSolidColor) {
    const int width = 100;
    const int height = 100;
    std::vector<uint8_t> pixels(width * height * 4);

    // Fill with solid blue
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 0;       // R
        pixels[i + 1] = 0;   // G
        pixels[i + 2] = 255; // B
        pixels[i + 3] = 255; // A
    }

    auto png = PNGEncoder::Encode(pixels, width, height);

    // Solid colors should compress very well
    EXPECT_GT(png.size(), 0);
    EXPECT_LT(png.size(), pixels.size() / 10); // Should compress to <10% of original
}

// Test encoding with transparency
TEST(PNGEncoderTest, EncodeWithTransparency) {
    const int width = 50;
    const int height = 50;
    std::vector<uint8_t> pixels(width * height * 4);

    // Fill with semi-transparent pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            pixels[idx] = 255;                                  // R
            pixels[idx + 1] = 255;                              // G
            pixels[idx + 2] = 255;                              // B
            pixels[idx + 3] = static_cast<uint8_t>((x + y) % 256); // A (varying transparency)
        }
    }

    auto png = PNGEncoder::Encode(pixels, width, height);

    EXPECT_GT(png.size(), 0);
    EXPECT_EQ(png[0], 0x89); // Verify PNG header
}

// Test error handling - invalid dimensions
TEST(PNGEncoderTest, InvalidDimensions) {
    std::vector<uint8_t> pixels(100);

    // Zero width
    EXPECT_THROW(PNGEncoder::Encode(pixels, 0, 10), std::runtime_error);

    // Zero height
    EXPECT_THROW(PNGEncoder::Encode(pixels, 10, 0), std::runtime_error);

    // Negative dimensions
    EXPECT_THROW(PNGEncoder::Encode(pixels, -10, 10), std::runtime_error);
    EXPECT_THROW(PNGEncoder::Encode(pixels, 10, -10), std::runtime_error);
}

// Test error handling - size mismatch
TEST(PNGEncoderTest, PixelSizeMismatch) {
    std::vector<uint8_t> pixels(100); // Too small for 10x10 RGBA

    EXPECT_THROW(PNGEncoder::Encode(pixels, 10, 10), std::runtime_error);
}

// Test minimum size image (1x1)
TEST(PNGEncoderTest, MinimumSizeImage) {
    std::vector<uint8_t> pixels = {255, 0, 0, 255}; // 1x1 red pixel

    auto png = PNGEncoder::Encode(pixels, 1, 1);

    EXPECT_GT(png.size(), 0);
    EXPECT_EQ(png[0], 0x89); // Verify PNG header
}
