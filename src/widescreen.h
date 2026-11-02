// [lttp] Widescreen: the neighbouring overworld areas and dungeon rooms in the space at the
// sides.
#pragma once
#include "types.h"

// Whether the overworld goes on past the current area's edges into its neighbours.
bool Widescreen_OverworldShowsNeighbors(void);
// Outdoors, where the world (or a special area) ends less than the extra space past the
// original's picture, how far the picture is moved off its edge (positive at the left edge,
// negative at the right): the camera stops at the edge, as in a game made widescreen, and
// the game's picture is drawn that far towards it (Ppu.viewShift). Indoors, as
// Widescreen_EndFrame last had it. Else 0.
int Widescreen_ViewShift(void);
// And indoors, where what's past the room's edge isn't drawn, the picture moved away from
// it, the camera stopping short of the edge: worked out at the end of each frame, which
// Widescreen_Reset does too after a load.
void Widescreen_EndFrame(void);
void Widescreen_Reset(void);
// Indoors, how far the picture goes on to each side and below, on into the rooms next door
// as far as they're drawn (if they aren't, upstream's extents, kept to the room the
// camera's in), from where it's moved to, and how far that is (Ppu.viewShift). Where it
// goes on less in some lines than others (rooms above and below whose sides differ), the
// most of any, and each line's own in Ppu.extraLeftLine and extraRightLine.
int Widescreen_DungeonSideSpace(int *left, int *right, int *bottom);
// Around the PPU's drawing of a frame: put the neighbours' tiles in the tilemaps past the
// area's or room's edges, then put back what was there.
void Widescreen_DrawNeighbors(void);
void Widescreen_Restore(void);
