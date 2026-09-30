#pragma once

#include <GfxRenderer.h>
#include <HalMemory.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "components/themes/BaseTheme.h"

// The Image widget's pictures: BMP, PNG and JPEG (camera photos are JPEG;
// large ones decode at a reduced scale first, see JpegToFramebufferConverter).
namespace dashboard_image {

// Contain: all of the picture, letterboxed, never enlarged. Fill: cropped to
// the area's shape (centred) and scaled to cover it exactly, small pictures
// enlarged; a BMP is only ever shrunk (drawBitmap), so a small one sits centred.
enum class Fit : uint8_t { Contain, Fill };

// A file the Image widget shows: not hidden, .bmp, .png, .jpg or .jpeg.
bool isImage(std::string_view name);

// Draws the file into area, centred, into whatever the renderer draws to: the
// 1bpp frame (dithered) or an active 16-level target (grey, see
// drawFullScreen). False when it cannot be read.
bool draw(GfxRenderer& renderer, const Rect& area, const std::string& path, Fit fit = Fit::Contain);

// The whole screen: grey through the 16-level target where the panel has one
// (the IT8951), else dithered black and white; shown with a clean refresh.
// Leaves the 1bpp frame holding the picture's base for later overlays.
bool drawFullScreen(GfxRenderer& renderer, const std::string& path);

// Whitens the corners of rect outside the outline drawRoundedRect(rect, ...,
// radius) strokes (the same arc, row by row), so a picture filling rect is
// clipped by the frame drawn over it. Works on the 1bpp frame and the 16-level
// target alike (fillRect's white).
void maskCorners(const GfxRenderer& renderer, const Rect& rect, int radius);

// One tile's finished pixels in PSRAM, so a repaint (a focus move) blits them
// back instead of reading and decoding the picture again. Held in the panel's
// physical layout of whichever frame they were drawn into: the 16-level
// target's nibbles or the 1bpp frame's bits. Render task only; invalidate()
// from elsewhere under the render lock.
class TileCache {
 public:
  // Blits the held pixels into rect when they are path's, drawn at rect in the
  // renderer's current orientation into the same kind of frame the renderer
  // draws to now. False, nothing drawn, otherwise.
  bool restore(const GfxRenderer& renderer, const Rect& rect, const std::string& path) const;
  // Keeps rect's pixels, just drawn for path, out of the frame being drawn.
  // Reuses the buffer when it is big enough. False when PSRAM cannot hold them
  // (the tile then decodes on every paint).
  bool capture(const GfxRenderer& renderer, const Rect& rect, const std::string& path);
  // Forgets the pixels; the buffer stays for the next capture.
  void invalidate() { valid = false; }
  // Frees the buffer.
  void release();

 private:
  struct Key {
    size_t pathHash = 0;
    Rect rect;
    int orientation = -1;
    bool gray4 = false;
  };
  bool matches(const GfxRenderer& renderer, const Rect& rect, const std::string& path) const;
  Key keyFor(const GfxRenderer& renderer, const Rect& rect, const std::string& path) const;

  HalMemory::PsramBuffer pixels;
  size_t capacity = 0;
  size_t failedBytes = 0;  // the last size PSRAM refused, logged once
  Key key;
  bool valid = false;
};

}  // namespace dashboard_image
