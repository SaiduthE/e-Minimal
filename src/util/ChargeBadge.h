#pragma once

class GfxRenderer;
class HalPowerManager;

// e-Minimal's charging badge over a sleep screen: a white square in the middle
// of the panel with a bolt and the charge level, refreshed on its own so the
// rest of the glass keeps the sleep screen.
namespace charge_badge {
void show(GfxRenderer& renderer, const HalPowerManager& power);
}  // namespace charge_badge
