#pragma once
#define PGE_PROFILE_LARGE 1
#define PGE_LOGICAL_W 320  // landscape; the panel is portrait, display_ltdc transposes (PGE_DISP_TRANSPOSED)
#define PGE_LOGICAL_H 240

// S8.2: external SDRAM (8 MB, brought up by BSP_LCD_Init inside pge::init) for big game buffers, e.g. a planet's change
// log. Section .sdram is NOLOAD: startup never copies or zeroes it, so the owner memsets it after pge::init(). Plain
// arrays only (no objects with constructors).
#define PGE_RAM_EXT __attribute__((section(".sdram")))
