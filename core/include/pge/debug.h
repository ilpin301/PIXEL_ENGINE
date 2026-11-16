#pragma once
#include <stdint.h>
#include "pge/engine.h"
#include "pge/ui.h"

namespace pge {

enum OverlayFlags : uint8_t { kOverlayTiming = 1, kOverlayPools = 2, kOverlayAll = 3 };

// Text panel at the top-left of the canvas (max 16 lines of 40 chars). Call it last in onRender; every band redraws it.
// Does nothing when flags == 0 or when PGE_DEBUG_OVERLAY is 0.
void drawOverlay(Canvas& c, const ui::Style& s, const Stats& st, uint8_t flags);

}  // namespace pge
