// [lttp] Widescreen on the overworld: the neighbouring areas in the space at the sides.
#pragma once
#include "types.h"

// Whether the overworld goes on past the current area's edges into its neighbours.
bool Widescreen_OverworldShowsNeighbors(void);
// Around the PPU's drawing of a frame: put the neighbours' tiles in BG2's tilemap past the
// area's edges, then put back what was there.
void Widescreen_OverworldDrawNeighbors(void);
void Widescreen_OverworldRestore(void);
