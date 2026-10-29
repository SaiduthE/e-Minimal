#include "ChargeBadge.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_EMINIMAL
#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include <cstdio>

#include "fontIds.h"

namespace charge_badge {
namespace {
// Panel pixels, a multiple of 32 on a 32-px column: an IT8951 bitmap-mode
// refresh widens its box to 32-px columns, and any column it widens over is
// driven from a frame buffer that does not hold the sleep screen.
constexpr int SIDE = 416;
constexpr int BORDER_INSET = 8;
constexpr int BORDER_WIDTH = 6;
constexpr int BORDER_RADIUS = 40;
// Lucide's "zap" outline (pointed at both ends) on an 18 x 20 grid, drawn 10x.
constexpr int BOLT_X[] = {10, 0, 9, 8, 18, 9};
constexpr int BOLT_Y[] = {0, 12, 12, 20, 8, 8};
constexpr int BOLT_POINTS = sizeof(BOLT_X) / sizeof(BOLT_X[0]);
constexpr int BOLT_GRID_W = 18;
constexpr int BOLT_GRID_H = 20;
constexpr int BOLT_NUM = 10;
constexpr int BOLT_DEN = 1;
constexpr int BOLT_TOP = 44;  // from the badge's top edge
constexpr int LABEL_GAP = 28;
}  // namespace

void show(GfxRenderer& renderer, const HalPowerManager& power) {
  // Upright like the sleep screens it sits on.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int px = ((renderer.getDisplayWidth() - SIDE) / 2) & ~31;
  const int py = (renderer.getDisplayHeight() - SIDE) / 2;
  int x, y, w, h;
  renderer.panelRectToScreen(px, py, SIDE, SIDE, &x, &y, &w, &h);

  renderer.fillRect(x, y, w, h, false);
  renderer.drawRoundedRect(x + BORDER_INSET, y + BORDER_INSET, w - 2 * BORDER_INSET, h - 2 * BORDER_INSET,
                           BORDER_WIDTH, BORDER_RADIUS, true);

  const int boltW = BOLT_GRID_W * BOLT_NUM / BOLT_DEN;
  const int boltH = BOLT_GRID_H * BOLT_NUM / BOLT_DEN;
  const int bx = x + (w - boltW) / 2;
  const int by = y + BOLT_TOP;
  int xs[BOLT_POINTS];
  int ys[BOLT_POINTS];
  for (int i = 0; i < BOLT_POINTS; i++) {
    xs[i] = bx + BOLT_X[i] * BOLT_NUM / BOLT_DEN;
    ys[i] = by + BOLT_Y[i] * BOLT_NUM / BOLT_DEN;
  }
  renderer.fillPolygon(xs, ys, BOLT_POINTS, true);

  const BatteryMonitor battery;
  const BatteryMonitor::Status st = battery.readStatus();
  char label[8];
  snprintf(label, sizeof(label), "%u%%",
           static_cast<unsigned>(st.percentageKnown ? st.percentage : power.getBatteryPercentage()));
  const int labelW = renderer.getTextWidth(NOTOSERIF_24_FONT_ID, label, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSERIF_24_FONT_ID, x + (w - labelW) / 2, by + boltH + LABEL_GAP, label, true,
                    EpdFontFamily::BOLD);

  LOG_INF("BADGE", "Charging badge: %s", label);
  renderer.displayWindow(x, y, w, h);
}
}  // namespace charge_badge

#else
namespace charge_badge {
void show(GfxRenderer&, const HalPowerManager&) {}
}  // namespace charge_badge
#endif
