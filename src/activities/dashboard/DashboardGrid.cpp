#include "DashboardGrid.h"

#include "CrossPointSettings.h"

namespace dashboard_grid {
namespace {
// Panel-memory blocks the driver refreshes in.
constexpr int BLOCK_COLUMNS_PX = 32;
constexpr int BLOCK_ROWS_PX = 16;
static_assert(dashboard_layout::CELL_PX % BLOCK_COLUMNS_PX == 0 && dashboard_layout::CELL_PX % BLOCK_ROWS_PX == 0,
              "grid cells must be whole refresh blocks along either axis");

// How one logical screen axis runs through panel memory.
struct Axis {
  int block;      // the block size of the panel axis it lands on
  bool reversed;  // panel = total - 1 - logical
};

// From rotateCoordinates() (GfxRenderer.cpp): which panel axis each logical
// axis becomes, and in which direction.
void axesFor(const GfxRenderer::Orientation orientation, Axis& x, Axis& y) {
  switch (orientation) {
    case GfxRenderer::Orientation::LandscapeCounterClockwise:  // panel (x, y)
      x = {BLOCK_COLUMNS_PX, false};
      y = {BLOCK_ROWS_PX, false};
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:  // panel (W-1-x, H-1-y)
      x = {BLOCK_COLUMNS_PX, true};
      y = {BLOCK_ROWS_PX, true};
      break;
    case GfxRenderer::Orientation::Portrait:  // panel (y, H-1-x)
      x = {BLOCK_ROWS_PX, true};
      y = {BLOCK_COLUMNS_PX, false};
      break;
    case GfxRenderer::Orientation::PortraitInverted:  // panel (W-1-y, x)
    default:
      x = {BLOCK_ROWS_PX, false};
      y = {BLOCK_COLUMNS_PX, true};
      break;
  }
}

// The grid's offset along one axis: centred as near as possible with its
// cell edges on block boundaries, measured from the far edge when the axis
// runs backwards through panel memory.
int alignedOrigin(const int total, const int grid, const Axis axis) {
  const int centred = (total - grid) / 2;
  if (!axis.reversed) return centred - centred % axis.block;
  int panelStart = total - centred - grid;
  panelStart -= panelStart % axis.block;
  return total - grid - panelStart;
}

// One tile's span along an axis: inner edges on the aligned grid, the outer
// ones stretched to the viewable edge.
void span(const int cell, const int cells, const int gridCells, const int origin, const int low, const int high,
          int& start, int& length) {
  const int first = cell == 0 ? low : origin + cell * dashboard_layout::CELL_PX;
  const int last = cell + cells >= gridCells ? high : origin + (cell + cells) * dashboard_layout::CELL_PX;
  start = first;
  length = last - first;
}
}  // namespace

bool applyOrientation(GfxRenderer& renderer) {
  switch (SETTINGS.dashboardOrientation) {
    case CrossPointSettings::DASHBOARD_PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      return true;
    case CrossPointSettings::DASHBOARD_LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      return false;
    case CrossPointSettings::DASHBOARD_LANDSCAPE_CW:
    default:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      return false;
  }
}

bool isPortrait(const GfxRenderer& renderer) {
  const auto orientation = renderer.getOrientation();
  return orientation == GfxRenderer::Orientation::Portrait ||
         orientation == GfxRenderer::Orientation::PortraitInverted;
}

Rect tileRect(const GfxRenderer& renderer, const DashboardTile& tile) {
  const bool portrait = isPortrait(renderer);
  const int columns = dashboard_layout::columns(portrait);
  const int rows = dashboard_layout::rows(portrait);
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  Axis xAxis;
  Axis yAxis;
  axesFor(renderer.getOrientation(), xAxis, yAxis);
  const int originX = alignedOrigin(width, columns * dashboard_layout::CELL_PX, xAxis);
  const int originY = alignedOrigin(height, rows * dashboard_layout::CELL_PX, yAxis);

  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);

  Rect rect{};
  span(tile.x, tile.width, columns, originX, left, width - right, rect.x, rect.width);
  span(tile.y, tile.height, rows, originY, top, height - bottom, rect.y, rect.height);
  return rect;
}

}  // namespace dashboard_grid
