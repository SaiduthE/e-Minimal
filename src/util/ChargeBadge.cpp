#include "ChargeBadge.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_EMINIMAL
#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <type_traits>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace charge_badge {
namespace {
// Per badge size, in panel pixels. SIDE is a multiple of 32: an IT8951
// bitmap-mode refresh widens its box to 32-px columns, and any column it widens
// over is driven from a frame buffer that does not hold the sleep screen.
struct SizeSpec {
  int side;
  int boltScale;  // bolt grid units to pixels
  int boltTop;    // bolt's top edge below the badge's top, with a label
  int labelGap;   // bolt bottom to label top
  int inset;      // border inset
  int borderWidth;
  int borderRadius;
  int labelFont;
};
constexpr SizeSpec SIZES[] = {
    {416, 10, 44, 28, 8, 6, 40, NOTOSERIF_24_FONT_ID},  // BADGE_LARGE
    {320, 8, 34, 22, 6, 5, 32, NOTOSERIF_22_FONT_ID},   // BADGE_MEDIUM
    {224, 5, 24, 15, 4, 4, 22, NOTOSERIF_20_FONT_ID},   // BADGE_SMALL
};
static_assert(std::size(SIZES) == CrossPointSettings::CHARGE_BADGE_SIZE_COUNT, "one spec per badge size");
static_assert(SIZES[0].side % 32 == 0 && SIZES[1].side % 32 == 0 && SIZES[2].side % 32 == 0,
              "badge sides must be whole IT8951 columns");

// Percent alone has the room for the biggest serif the board carries.
constexpr int PERCENT_ONLY_FONT = NOTOSERIF_24_FONT_ID;
// Distance to the panel edge for the corner positions (a multiple of 32, so the
// top corners stay column-aligned).
constexpr int EDGE_MARGIN = 64;

// Lucide's "zap" outline (pointed at both ends) on an 18 x 20 grid.
constexpr int BOLT_X[] = {10, 0, 9, 8, 18, 9};
constexpr int BOLT_Y[] = {0, 12, 12, 20, 8, 8};
constexpr int BOLT_POINTS = sizeof(BOLT_X) / sizeof(BOLT_X[0]);
constexpr int BOLT_GRID_W = 18;
constexpr int BOLT_GRID_H = 20;

// Panel pixels of the badge's top-left corner. The corner is chosen on the
// Portrait screen as the sleep image is seen, then mapped to the panel by the
// renderer, which also accounts for a panel mounted upside down.
void placeBadge(const GfxRenderer& renderer, const int position, const int side, int* px, int* py) {
  using S = CrossPointSettings;
  const bool top = position == S::BADGE_TOP_LEFT || position == S::BADGE_TOP_RIGHT;
  const bool bottom = position == S::BADGE_BOTTOM_LEFT || position == S::BADGE_BOTTOM_RIGHT;
  const bool left = position == S::BADGE_TOP_LEFT || position == S::BADGE_BOTTOM_LEFT;
  const bool right = position == S::BADGE_TOP_RIGHT || position == S::BADGE_BOTTOM_RIGHT;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int sx = left ? EDGE_MARGIN : (right ? screenW - side - EDGE_MARGIN : (screenW - side) / 2);
  const int sy = top ? EDGE_MARGIN : (bottom ? screenH - side - EDGE_MARGIN : (screenH - side) / 2);
  int pw, ph;
  renderer.screenRectToPanel(sx, sy, side, side, px, py, &pw, &ph);
  // x stays on a 32-px column in every case; y needs no alignment.
  *px &= ~31;
}

// The badge square in panel pixels for the current settings: the one box the
// sleep entry snapshots and the wake redraws.
struct Box {
  int x, y, side;
  uint8_t sizeIdx;
  uint8_t position;
};

// Leaves the renderer in Portrait, the orientation the sleep image is drawn in.
Box badgeBox(GfxRenderer& renderer) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Box b;
  b.sizeIdx = SETTINGS.chargeBadgeSize < CrossPointSettings::CHARGE_BADGE_SIZE_COUNT ? SETTINGS.chargeBadgeSize : 0;
  b.position = SETTINGS.chargeBadgePosition;
  b.side = SIZES[b.sizeIdx].side;
  placeBadge(renderer, b.position, b.side, &b.x, &b.y);
  return b;
}

// What lies under the badge, as the sleep screen left it on the glass: the IT8951
// loses its memory in deep sleep, so the pixels under the badge are kept on the
// SD card and the wake puts them back around the badge's rounded border.
constexpr char WINDOW_FILE[] = "/.crosspoint/badge_window.bin";
constexpr uint32_t WINDOW_MAGIC = 0x57444742;  // "BGDW"
constexpr uint16_t WINDOW_VERSION = 1;

struct WindowHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t x, y, side;
  uint8_t sizeIdx;
  uint8_t position;
  uint32_t dataSize;  // side * side / 2: packed 4bpp, left pixel in the high nibble
};
static_assert(sizeof(WindowHeader) == 20 && std::is_trivially_copyable_v<WindowHeader>, "header is written raw");

// PSRAM pixel buffer, freed on every exit path. heap_caps_malloc because the
// 86 KB of the largest badge does not belong in internal RAM.
struct PsramBuf {
  uint8_t* p;
  explicit PsramBuf(const size_t n) : p(static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM))) {}
  ~PsramBuf() { heap_caps_free(p); }
  PsramBuf(const PsramBuf&) = delete;
  PsramBuf& operator=(const PsramBuf&) = delete;
};

constexpr size_t windowBytes(const int side) { return static_cast<size_t>(side) * side / 2; }

// Reads the saved window into buf (windowBytes(box.side) bytes) when the file is
// there and was taken for this very box.
bool loadWindow(const Box& box, uint8_t* buf) {
  HalFile file;
  if (!Storage.openFileForRead("BADGE", WINDOW_FILE, file)) return false;
  WindowHeader hdr;
  const size_t want = windowBytes(box.side);
  if (file.read(&hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr)) || hdr.magic != WINDOW_MAGIC ||
      hdr.version != WINDOW_VERSION || hdr.x != box.x || hdr.y != box.y || hdr.side != box.side ||
      hdr.sizeIdx != box.sizeIdx || hdr.position != box.position || hdr.dataSize != want) {
    LOG_ERR("BADGE", "Saved window does not fit this badge");
    return false;
  }
  if (file.read(buf, want) != static_cast<int>(want)) {
    LOG_ERR("BADGE", "Saved window is short");
    return false;
  }
  return true;
}

// Row source for displayWindowGray4: the saved pixel, except where `fb` is set
// and the pixel lies inside the badge's outer rounded rectangle, which takes the
// framebuffer's black or white. All in panel coordinates, like the box.
struct FillCtx {
  const uint8_t* saved;
  const uint8_t* fb;  // panel-layout 1bpp frame, a set bit white; null = saved pixels only
  int x, y, side;
  int inset, radius;
  int fbStride;  // bytes per framebuffer row
};

bool insideBadge(const FillCtx& c, const int u, const int v) {
  const int s = c.side - 2 * c.inset;
  const int cu = u - c.inset;
  const int cv = v - c.inset;
  if (cu < 0 || cv < 0 || cu >= s || cv >= s) return false;
  // Corner arcs, a pixel generous so the border's own edge stays the badge's.
  const int r = c.radius;
  const int du = cu < r ? r - cu : (cu >= s - r ? cu - (s - r - 1) : 0);
  const int dv = cv < r ? r - cv : (cv >= s - r ? cv - (s - r - 1) : 0);
  if (du == 0 || dv == 0) return true;
  const int a = 2 * du - 1;
  const int b = 2 * dv - 1;
  const int lim = 2 * r + 2;
  return a * a + b * b <= lim * lim;
}

void fillWindowRow(void* ctx, const uint16_t row, uint8_t* dst) {
  const FillCtx& c = *static_cast<const FillCtx*>(ctx);
  const uint8_t* src = c.saved + static_cast<size_t>(row) * (c.side / 2);
  if (!c.fb) {
    memcpy(dst, src, c.side / 2);
    return;
  }
  const uint8_t* fbRow = c.fb + static_cast<size_t>(c.y + row) * c.fbStride;
  for (int i = 0; i < c.side / 2; i++) {
    uint8_t out = 0;
    for (int k = 0; k < 2; k++) {
      const int u = i * 2 + k;
      uint8_t v = k ? (src[i] & 0x0F) : (src[i] >> 4);
      if (insideBadge(c, u, row)) {
        const int px = c.x + u;
        v = (fbRow[px >> 3] & (0x80 >> (px & 7))) ? 0xF : 0x0;
      }
      out = static_cast<uint8_t>((out << 4) | v);
    }
    dst[i] = out;
  }
}
}  // namespace

void show(GfxRenderer& renderer, const HalPowerManager& power, HalDisplay& display) {
  const Box box = badgeBox(renderer);
  const uint8_t sizeIdx = box.sizeIdx;
  const SizeSpec& sz = SIZES[sizeIdx];
  const bool showBolt = SETTINGS.chargeBadgeStyle != CrossPointSettings::BADGE_PERCENT_ONLY;
  const bool showLabel = SETTINGS.chargeBadgeStyle != CrossPointSettings::BADGE_BOLT_ONLY;
  // Inverted is white ink on a black square; draw calls take black = true.
  const bool bg = SETTINGS.chargeBadgeInverted != 0;

  // Upright like the sleep screens it sits on.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  int x, y, w, h;
  renderer.panelRectToScreen(box.x, box.y, sz.side, sz.side, &x, &y, &w, &h);

  renderer.fillRect(x, y, w, h, bg);
  renderer.drawRoundedRect(x + sz.inset, y + sz.inset, w - 2 * sz.inset, h - 2 * sz.inset, sz.borderWidth,
                           sz.borderRadius, !bg);

  char label[8];
  int labelFont = sz.labelFont;
  if (showLabel) {
    const BatteryMonitor battery;
    const BatteryMonitor::Status st = battery.readStatus();
    snprintf(label, sizeof(label), "%u%%",
             static_cast<unsigned>(st.percentageKnown ? st.percentage : power.getBatteryPercentage()));
    if (!showBolt) labelFont = PERCENT_ONLY_FONT;
  } else {
    label[0] = '\0';
  }

  const int boltW = BOLT_GRID_W * sz.boltScale;
  const int boltH = BOLT_GRID_H * sz.boltScale;
  const int labelH = showLabel ? renderer.getLineHeight(labelFont) : 0;
  // Bolt and label keep the spacing from the table; one alone is centred.
  int boltY = y + sz.boltTop;
  int labelY = boltY + boltH + sz.labelGap;
  if (!showLabel) {
    boltY = y + (h - boltH) / 2;
  } else if (!showBolt) {
    labelY = y + (h - labelH) / 2;
  }

  if (showBolt) {
    const int bx = x + (w - boltW) / 2;
    int xs[BOLT_POINTS];
    int ys[BOLT_POINTS];
    for (int i = 0; i < BOLT_POINTS; i++) {
      xs[i] = bx + BOLT_X[i] * sz.boltScale;
      ys[i] = boltY + BOLT_Y[i] * sz.boltScale;
    }
    renderer.fillPolygon(xs, ys, BOLT_POINTS, !bg);
  }
  if (showLabel) {
    const int labelW = renderer.getTextWidth(labelFont, label, EpdFontFamily::BOLD);
    renderer.drawText(labelFont, x + (w - labelW) / 2, labelY, label, !bg, EpdFontFamily::BOLD);
  }

  LOG_INF("BADGE", "Charging badge: size %u pos %u style %u '%s'", static_cast<unsigned>(sizeIdx),
          static_cast<unsigned>(SETTINGS.chargeBadgePosition), static_cast<unsigned>(SETTINGS.chargeBadgeStyle), label);

  // The sleep image's pixels around the border come from the SD card, so the
  // square's corners do not show as a plain patch; any miss falls back to the
  // plain square.
  PsramBuf saved(windowBytes(box.side));
  if (saved.p && loadWindow(box, saved.p)) {
    FillCtx ctx = {saved.p, renderer.getFrameBuffer(), box.x, box.y, box.side, sz.inset, sz.borderRadius,
                 renderer.getDisplayWidthBytes()};
    if (display.displayWindowGray4(box.x, box.y, box.side, box.side, fillWindowRow, &ctx)) return;
    LOG_ERR("BADGE", "Window refresh refused, plain badge");
  } else if (!saved.p) {
    LOG_ERR("BADGE", "No memory for the saved window, plain badge");
  }
  renderer.displayWindow(x, y, w, h);
}

void saveWindow(GfxRenderer& renderer, HalDisplay& display) {
  if (!SETTINGS.chargeBadgeEnabled) {
    if (Storage.exists(WINDOW_FILE)) Storage.remove(WINDOW_FILE);
    return;
  }
  const Box box = badgeBox(renderer);
  PsramBuf pixels(windowBytes(box.side));
  if (!pixels.p) {
    LOG_ERR("BADGE", "No memory to save the window");
  } else if (display.snapshotWindowGray4(box.x, box.y, box.side, box.side, pixels.p)) {
    const WindowHeader hdr = {WINDOW_MAGIC,
                              WINDOW_VERSION,
                              static_cast<uint16_t>(box.x),
                              static_cast<uint16_t>(box.y),
                              static_cast<uint16_t>(box.side),
                              box.sizeIdx,
                              box.position,
                              static_cast<uint32_t>(windowBytes(box.side))};
    HalFile file;
    if (Storage.openFileForWrite("BADGE", WINDOW_FILE, file)) {
      const bool ok = file.write(&hdr, sizeof(hdr)) == sizeof(hdr) &&
                      file.write(pixels.p, windowBytes(box.side)) == windowBytes(box.side);
      if (ok) return;
      LOG_ERR("BADGE", "Window write failed");
      file.close();
    } else {
      LOG_ERR("BADGE", "Cannot open the window file");
    }
  }
  // Nothing trustworthy to keep: a stale file would put an older image back.
  if (Storage.exists(WINDOW_FILE)) Storage.remove(WINDOW_FILE);
}

bool restoreWindow(GfxRenderer& renderer, HalDisplay& display) {
  const Box box = badgeBox(renderer);
  PsramBuf saved(windowBytes(box.side));
  if (!saved.p || !loadWindow(box, saved.p)) return false;
  FillCtx ctx = {saved.p, nullptr, box.x, box.y, box.side, 0, 0, 0};
  return display.displayWindowGray4(box.x, box.y, box.side, box.side, fillWindowRow, &ctx);
}
}  // namespace charge_badge

#else
namespace charge_badge {
void show(GfxRenderer&, const HalPowerManager&, HalDisplay&) {}
void saveWindow(GfxRenderer&, HalDisplay&) {}
bool restoreWindow(GfxRenderer&, HalDisplay&) { return false; }
}  // namespace charge_badge
#endif
