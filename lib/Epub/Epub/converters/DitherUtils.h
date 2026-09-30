#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Apply Bayer dithering and quantize to 4 levels (0-3)
// Stateless - works correctly with any pixel processing order
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 5;  // Scale to +/-40 (half of quantization step 85)

  int adjusted = gray + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 64) return 0;
  if (adjusted < 128) return 1;
  if (adjusted < 192) return 2;
  return 3;
}

// Ordered dither to 16 levels (0 black .. 15 white) for the 4bpp target. One
// level step is 17 grey values; the Bayer rank splits it into 16 thresholds
// (7 .. 247 of 255), so a flat grey becomes a fine mix of its two nearest
// levels with the right mean. Stateless, like applyBayerDither4Level.
inline uint8_t applyBayerDither16Level(uint8_t gray, int x, int y) {
  const int threshold = ((bayer4x4[y & 3][x & 3] * 2 + 1) * 255) >> 5;
  return static_cast<uint8_t>((gray * 15 + threshold) / 255);
}

// Nearest of the 16 levels, no dither.
inline uint8_t quantize16Level(uint8_t gray) { return static_cast<uint8_t>((gray * 15 + 127) / 255); }
