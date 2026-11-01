// [lttp] Widescreen: the neighbouring overworld areas and dungeon rooms in the space at the
// sides.
#pragma once
#include "types.h"

// Whether the overworld goes on past the current area's edges into its neighbours.
bool Widescreen_OverworldShowsNeighbors(void);
// Outdoors, where the world (or a special area) ends less than the extra space past the
// original's picture, how far the picture is moved off its edge (positive at the left edge,
// negative at the right): the camera stops at the edge, as in a game made widescreen, and
// the game's picture is drawn that far towards it (Ppu.viewShift). Else 0.
int Widescreen_ViewShift(void);
// Indoors, how far the picture goes on to each side and below, on into the rooms next door
// as far as they're drawn: false if they aren't, and then upstream's extents stand, kept
// to the room the camera's in.
bool Widescreen_DungeonSideSpace(int *left, int *right, int *bottom);
// Around the PPU's drawing of a frame: put the neighbours' tiles in the tilemaps past the
// area's or room's edges, then put back what was there.
void Widescreen_DrawNeighbors(void);
void Widescreen_Restore(void);
