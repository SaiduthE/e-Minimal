#pragma once

class GfxRenderer;
class HalDisplay;
class HalPowerManager;

// e-Minimal's charging badge over a sleep screen: a white square in the middle
// of the panel with a bolt and the charge level, refreshed on its own so the
// rest of the glass keeps the sleep screen.
namespace charge_badge {
// Draws the badge into the renderer's frame and refreshes its square. With the
// window saved at sleep entry, the pixels around the rounded border are the
// sleep image's; otherwise the whole square is the badge's.
void show(GfxRenderer& renderer, const HalPowerManager& power, HalDisplay& display);
// Sleep entry, after the sleep screen is on the glass: keep the pixels under the
// badge square on the SD card (or drop a stale file when they cannot be kept or
// the badge is off).
void saveWindow(HalDisplay& display);
// Puts the saved window back as it was. False, glass untouched, when it cannot.
bool restoreWindow(HalDisplay& display);
}  // namespace charge_badge
