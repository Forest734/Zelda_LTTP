// [lttp] Widescreen on the overworld: the areas next to the current one, drawn in the space
// at the sides past its edges, where upstream shows black.
//
// The game keeps only the current area's map in RAM (dung_bg2) and streams it into BG2's
// 64x64 tilemap as the camera moves, which holds the world mod 512 pixels; past the area's
// edges the tilemap holds whatever was left there. A neighbour's map16 is built by the
// game's own code, as on entering it (Overworld_DrawQuadrantsAndOverlays: its screens from
// the ROM, then its event overlay and bombed doors as the save has them), on a copy of RAM
// that's then put back, and kept until its events change. Just before the PPU draws a
// frame, the tiles past the area's edges that are in the extra space (at the sides, and
// at the bottom with extend_y) get the neighbours', and once it's drawn they get back what
// they held: the game's RAM and VRAM stay as they were, so nothing it does changes, and
// the original's 256x224 is always the game's own.
//
// The neighbours are drawn with the current area's graphics and palette, as the original
// draws the next area while scrolling over to it, and without their sprites.
#include "zelda_rtl.h"
#include "variables.h"
#include "overworld.h"
#include "widescreen.h"
#include "snes/ppu.h"

enum { kWorldSize = 0x1000 };  // either world is 8x8 screens of 512 pixels

typedef struct AreaMap {
  uint16 *map16;  // 64x64 from the area's top left, as dung_bg2
  uint8 events;   // its save_ow_event_info when built
} AreaMap;
static AreaMap g_area_maps[128];  // by the area's top left screen, light world then dark

static const uint16 *AreaMap16(int area) {
  AreaMap *a = &g_area_maps[area];
  uint8 events = save_ow_event_info[area];
  if (a->map16 && a->events == events)
    return a->map16;
  static uint8 ram[sizeof(g_ram)];
  memcpy(ram, g_ram, sizeof(g_ram));
  overworld_screen_index = area;
  Overworld_DecompressAndDrawAllQuadrants();
  Overworld_HandleOverlaysAndBombDoors();
  if (!a->map16)
    a->map16 = malloc(64 * 64 * sizeof(uint16));
  memcpy(a->map16, dung_bg2, 64 * 64 * sizeof(uint16));
  memcpy(g_ram, ram, sizeof(g_ram));
  a->events = events;
  return a->map16;
}

// The map16 at a place in the current world, as its area is when entered.
static uint16 WorldMap16(int x, int y) {
  int screen = (y >> 9) << 3 | x >> 9;
  int ax = kOverworld_OffsetBaseX[screen], ay = kOverworld_OffsetBaseY[screen];
  int area = (overworld_screen_index & 0x40) | (ay >> 9) << 3 | ax >> 9;
  return AreaMap16(area)[((y - ay) >> 4) * 64 + ((x - ax) >> 4)];
}

bool Widescreen_OverworldShowsNeighbors(void) {
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || g_zenv.ppu->extraLeftRight == 0)
    return false;
  int mod = main_module_index == 14 ? saved_module_for_menu : main_module_index;
  if (mod != 9 || (main_module_index == 14 && submodule_index == 7 && overworld_map_state >= 4))
    return false;  // not outdoors, or the world map
  if (overworld_screen_index >= 0x80)
    return false;  // the special areas (the Master Sword's grove, Zora's waterfall...)
  const BgLayer *bg2 = &g_zenv.ppu->bgLayer[1];
  return bg2->tilemapWider && bg2->tilemapHigher;  // the 64x64 map, as the game sets it
}

// What Widescreen_OverworldDrawNeighbors wrote over, to put back.
static uint16 g_saved_adr[4096], g_saved_val[4096];
static int g_saved_count;

void Widescreen_OverworldDrawNeighbors(void) {
  g_saved_count = 0;
  if (!Widescreen_OverworldShowsNeighbors())
    return;
  Ppu *ppu = g_zenv.ppu;
  int extra = ppu->extraLeftRight;
  int hs = BG2HOFS_copy2, vs = BG2VOFS_copy2;
  // the area: 512 or 1024 pixels square, its camera kept 256x224 inside
  int area_size = ow_scroll_vars0.xend - ow_scroll_vars0.xstart + 256;
  int area_left = ow_scroll_vars0.xstart, area_right = area_left + area_size;
  int area_top = ow_scroll_vars0.ystart, area_bottom = area_top + area_size;
  const uint16 *map8 = GetMap16toMap8Table();
  uint16 base = ppu->bgLayer[1].tilemapAdr;

  int tx0 = IntMax(hs - extra, 0) >> 3, tx1 = IntMin(hs + 256 + extra + 7, kWorldSize) >> 3;
  int ty0 = IntMax(vs, 0) >> 3, ty1 = IntMin(vs + 240 + 7, kWorldSize) >> 3;  // 240: extend_y
  for (int tx = tx0; tx < tx1; tx++) {
    int x = tx * 8;
    for (int ty = ty0; ty < ty1 && g_saved_count < 4096; ty++) {
      int y = ty * 8;
      if (x + 8 > hs && x < hs + 256 && y + 8 > vs && y < vs + 224)
        continue;  // in the original's picture, if only partly: the game's own
      if (x >= area_left && x + 8 <= area_right && y >= area_top && y + 8 <= area_bottom)
        continue;  // in the area: the game's own
      uint16 tile = map8[WorldMap16(x, y) * 4 + (ty & 1) * 2 + (tx & 1)];
      uint16 adr = (base + ((ty & 31) << 5 | (tx & 31)) + (tx & 32 ? 0x400 : 0) + (ty & 32 ? 0x800 : 0)) & 0x7fff;
      g_saved_adr[g_saved_count] = adr;
      g_saved_val[g_saved_count++] = ppu->vram[adr];
      ppu->vram[adr] = tile;
    }
  }
}

void Widescreen_OverworldRestore(void) {
  while (g_saved_count > 0) {
    g_saved_count--;
    g_zenv.ppu->vram[g_saved_adr[g_saved_count]] = g_saved_val[g_saved_count];
  }
}
