#include "ChargeBadge.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_EMINIMAL
#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include <cstdio>
#include <iterator>

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

// Panel pixels of the badge's top-left corner. The screen is Portrait here, so
// panel X runs down the screen and panel Y runs right-to-left across it.
void placeBadge(const int position, const int side, const int panelW, const int panelH, int* px, int* py) {
  using S = CrossPointSettings;
  const bool top = position == S::BADGE_TOP_LEFT || position == S::BADGE_TOP_RIGHT;
  const bool bottom = position == S::BADGE_BOTTOM_LEFT || position == S::BADGE_BOTTOM_RIGHT;
  const bool left = position == S::BADGE_TOP_LEFT || position == S::BADGE_BOTTOM_LEFT;
  const bool right = position == S::BADGE_TOP_RIGHT || position == S::BADGE_BOTTOM_RIGHT;
  // x stays on a 32-px column in every case; y needs no alignment.
  if (top) {
    *px = EDGE_MARGIN;
  } else if (bottom) {
    *px = (panelW - side - EDGE_MARGIN) & ~31;
  } else {
    *px = ((panelW - side) / 2) & ~31;
  }
  if (left) {
    *py = panelH - side - EDGE_MARGIN;
  } else if (right) {
    *py = EDGE_MARGIN;
  } else {
    *py = (panelH - side) / 2;
  }
}
}  // namespace

void show(GfxRenderer& renderer, const HalPowerManager& power) {
  const uint8_t sizeIdx =
      SETTINGS.chargeBadgeSize < CrossPointSettings::CHARGE_BADGE_SIZE_COUNT ? SETTINGS.chargeBadgeSize : 0;
  const SizeSpec& sz = SIZES[sizeIdx];
  const bool showBolt = SETTINGS.chargeBadgeStyle != CrossPointSettings::BADGE_PERCENT_ONLY;
  const bool showLabel = SETTINGS.chargeBadgeStyle != CrossPointSettings::BADGE_BOLT_ONLY;
  // Inverted is white ink on a black square; draw calls take black = true.
  const bool bg = SETTINGS.chargeBadgeInverted != 0;

  // Upright like the sleep screens it sits on.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  int px, py;
  placeBadge(SETTINGS.chargeBadgePosition, sz.side, renderer.getDisplayWidth(), renderer.getDisplayHeight(), &px, &py);
  int x, y, w, h;
  renderer.panelRectToScreen(px, py, sz.side, sz.side, &x, &y, &w, &h);

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
  renderer.displayWindow(x, y, w, h);
}
}  // namespace charge_badge

#else
namespace charge_badge {
void show(GfxRenderer&, const HalPowerManager&) {}
}  // namespace charge_badge
#endif
