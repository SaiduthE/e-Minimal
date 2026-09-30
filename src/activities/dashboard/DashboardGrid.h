#pragma once

#include <GfxRenderer.h>

#include "DashboardConfigStore.h"
#include "components/themes/BaseTheme.h"

// Where the launched dashboard's tiles land on screen. The grid
// (dashboard_layout) is placed so every inner cell edge falls on a block
// boundary of panel memory -- 32-px columns and 16-row bands, the driver's
// partial-refresh granularity (It8951Driver BITMAP_COL_ALIGN / ROW_ALIGN) --
// in whichever orientation the dashboard is turned. The outer tiles then
// stretch to the panel's viewable edge, so the grid fills the screen.
namespace dashboard_grid {

// Turns the renderer the way the Dashboard Orientation setting says. True for portrait.
bool applyOrientation(GfxRenderer& renderer);
bool isPortrait(const GfxRenderer& renderer);

// A tile's rectangle on screen in the renderer's current orientation, from
// cell edge to cell edge (the frame is drawn inside).
Rect tileRect(const GfxRenderer& renderer, const DashboardTile& tile);

}  // namespace dashboard_grid
