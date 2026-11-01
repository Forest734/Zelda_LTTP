// [lttp] Widescreen: what's past the edges of the overworld area or the dungeon room Link is
// in, drawn in the space at the sides where upstream shows black.
//
// Just before the PPU draws a frame, the tiles in the extra space (at the sides, and at the
// bottom with extend_y) that are past the area's or room's edges are written into VRAM, and
// once it's drawn they get back what they held: the game's RAM and VRAM stay as they were,
// so nothing it does changes, and the original's 256x224 is always the game's own. What's
// next door is built by the game's own code, as on going in, on a copy of RAM that's then
// put back, and kept until what the save says of it changes. It's drawn with the current
// graphics and palette, as the original draws the next area or room while scrolling over to
// it, and without its sprites, which appear on going in, as on the SNES.
//
// The overworld: the game keeps only the current area's map in RAM (dung_bg2) and streams
// it into BG2's 64x64 tilemap as the camera moves, which holds the world mod 512 pixels;
// past the area's edges the tilemap holds whatever was left there. A neighbour's map16 is
// built as on entering it (Overworld_DrawQuadrantsAndOverlays: its screens from the ROM,
// then its event overlay and bombed doors as the save has them).
//
// Dungeons: a room is 512 pixels square, on a grid 16 rooms wide (room r at x = (r & 15) *
// 512, y = (r >> 4 & 15) * 512; rooms 0x100 on are a grid of their own), the one Link walks
// into past an edge the next on the grid. All of a room is in BG2's and BG1's 64x64
// tilemaps (dung_bg2 and dung_bg1 in RAM), so past its edges they hold its own other side,
// out of view (398 < 512): there go the rooms next door, built by Dungeon_LoadRoom as Link
// would find them coming in from this one, if they'd look right with this one's graphics,
// palette and layers. While the camera scrolls from one room to the next, the tilemaps
// change over to the new room bit by bit, and the old one is drawn from a copy of its tiles
// taken while Link was in it.
#include "zelda_rtl.h"
#include "variables.h"
#include "overworld.h"
#include "dungeon.h"
#include "widescreen.h"
#include "snes/ppu.h"

// What's written into VRAM for a frame, to put back once it's drawn.
static uint16 g_saved_adr[8192], g_saved_val[8192];
static int g_saved_count;

static void WriteTile(uint16 adr, uint16 tile) {
  if (g_saved_count == countof(g_saved_adr))
    return;
  g_saved_adr[g_saved_count] = adr;
  g_saved_val[g_saved_count++] = g_zenv.ppu->vram[adr];
  g_zenv.ppu->vram[adr] = tile;
}

// The VRAM word of tile (tx, ty) of a 64x64 tilemap at base, the world mod 512 pixels.
static uint16 TilemapAdr(uint16 base, int tx, int ty) {
  return (base + ((ty & 31) << 5 | (tx & 31)) + (tx & 32 ? 0x400 : 0) + (ty & 32 ? 0x800 : 0)) & 0x7fff;
}

static bool TilemapIs64x64(const BgLayer *bg) {
  return bg->tilemapWider && bg->tilemapHigher;
}

// A 10-bit scroll register's distance from where the game's camera is.
static int ScrollDelta(uint16 reg, uint16 camera) {
  return ((reg - camera + 512) & 0x3ff) - 512;
}

// ---- The overworld ----

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
  return TilemapIs64x64(&g_zenv.ppu->bgLayer[1]);  // as the game sets it
}

static void OverworldDrawNeighbors(void) {
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
    for (int ty = ty0; ty < ty1; ty++) {
      int y = ty * 8;
      if (x + 8 > hs && x < hs + 256 && y + 8 > vs && y < vs + 224)
        continue;  // in the original's picture, if only partly: the game's own
      if (x >= area_left && x + 8 <= area_right && y >= area_top && y + 8 <= area_bottom)
        continue;  // in the area: the game's own
      WriteTile(TilemapAdr(base, tx, ty), map8[WorldMap16(x, y) * 4 + (ty & 1) * 2 + (tx & 1)]);
    }
  }
}


// ---- Dungeons ----

enum { kRoomTiles = 64 * 64, kNumRooms = 0x140 };

typedef struct RoomMap {
  uint16 *bg;       // BG2's 64x64 tiles from the room's top left, then BG1's
  uint16 from;      // the room it was built as seen from
  uint16 saves[5];  // the save_dung_info of it and the rooms round it, then
} RoomMap;
static RoomMap g_room_maps[kNumRooms];

// The room last seen in RAM and VRAM, and its tiles then.
static uint16 g_seen_room = 0xffff;
static uint16 g_seen_bg[2 * kRoomTiles];

// This frame's: whether the rooms next door are drawn, the room whose own tiles are all in
// VRAM (-1 while scrolling over to the next, when it's some of each) and how much of it is
// shown, and the room they're to look like.
static bool g_dungeon_drawn;
static int g_own_room, g_own_x0, g_own_x1, g_own_y0, g_own_y1, g_like_room;

static const uint16 kInVram[1];  // RoomTiles: the game's own tiles are in VRAM

// The room at a place in the dungeon (the coordinates of BG2), or -1 off the grid.
static int RoomAt(int x, int y) {
  int col = x >> 9, row = y >> 9;
  if (col < 0 || col >= 16 || row < 0)
    return -1;
  if (dungeon_room_index < 0x100)
    return row < 16 ? row << 4 | col : -1;
  row &= 15;
  return row < (kNumRooms - 0x100) >> 4 ? 0x100 | row << 4 | col : -1;
}

// Whether room b looks right drawn with room a's graphics, palette and layers: its header
// has the same, and it isn't dark (which, unlit, is black anyway).
static bool RoomsLookAlike(int a, int b) {
  const uint8 *ha = GetRoomHeaderPtr(a), *hb = GetRoomHeaderPtr(b);
  return (hb[0] & 0xe1) == (ha[0] & 0xe0) && hb[2] == ha[2] &&
         GetDungPalInfo(hb[1])->pal0 == GetDungPalInfo(ha[1])->pal0;
}

const uint16 *GetRoomDoorInfo(int room);  // dungeon.c

// Whether rooms a and b are side by side with a door between them, one in each facing
// wall. A door's low byte is its position << 4 | the wall (0 north, 1 south, 2 west, 3
// east), and one at position p in a north or west wall meets one at p + 6 in the south or
// east wall of the room next door (Dungeon_CheckAdjacentRoomsForOpenDoors).
static bool RoomsJoined(int a, int b) {
  int wall;
  if (b == a - 1 && (a & 15) != 0)
    wall = 2;
  else if (b == a + 1 && (a & 15) != 15)
    wall = 3;
  else if (b == a - 16)
    wall = 0;
  else if (b == a + 16)
    wall = 1;
  else
    return false;
  const uint16 *da = GetRoomDoorInfo(a), *db = GetRoomDoorInfo(b);
  for (int i = 0; da[i] != 0xffff; i++) {
    if ((da[i] & 0xf) != wall)
      continue;
    int pos = da[i] >> 4 & 0xf;
    int other = (wall & 1 ? pos - 6 : pos + 6) << 4 | (wall ^ 1);
    for (int j = 0; db[j] != 0xffff; j++)
      if ((db[j] & 0xff) == other)
        return true;
  }
  return false;
}

// Whether room b is to be drawn next to room a. In a palace, the rooms on the grid round
// it are the rooms round it, which Link can walk into if he gets through the wall; houses
// and caves (cur_palace_index_x2 0xff) are put on the grid anyhow, so only one he can walk
// into through a door.
static bool RoomBesideRoom(int a, int b) {
  return RoomsLookAlike(a, b) && (cur_palace_index_x2 != 0xff || RoomsJoined(a, b));
}

static const uint16 *BuiltRoomTiles(int room, int from) {
  static const int8 kRound[5] = { 0, -1, 1, -16, 16 };
  uint16 saves[5];
  for (int i = 0; i < 5; i++) {
    int r = room + kRound[i];
    saves[i] = r >= 0 && r < kNumRooms ? save_dung_info[r] : 0;
  }
  RoomMap *m = &g_room_maps[room];
  if (m->bg && m->from == from && !memcmp(m->saves, saves, sizeof(saves)))
    return m->bg;
  static uint8 ram[sizeof(g_ram)];
  memcpy(ram, g_ram, sizeof(g_ram));
  dungeon_room_index_prev = from;  // the doors he'd come in by are open
  dungeon_room_index = room;
  Dungeon_LoadRoom();
  if (!m->bg)
    m->bg = malloc(2 * kRoomTiles * sizeof(uint16));
  memcpy(m->bg, dung_bg2, kRoomTiles * sizeof(uint16));
  memcpy(m->bg + kRoomTiles, dung_bg1, kRoomTiles * sizeof(uint16));
  memcpy(g_ram, ram, sizeof(g_ram));
  m->from = from;
  memcpy(m->saves, saves, sizeof(saves));
  return m->bg;
}

// Within a frame, the rooms looked up so far (the picture touches 4 at most).
static struct { int room; const uint16 *bg; } g_rooms_found[4];
static int g_num_rooms_found;

// A room's tiles, BG2's then BG1's, as they're to be drawn: kInVram if the game's own are
// in VRAM, NULL if it's not to be drawn.
static const uint16 *RoomTiles(int room) {
  if (room < 0)
    return NULL;
  for (int i = 0; i < g_num_rooms_found; i++)
    if (g_rooms_found[i].room == room)
      return g_rooms_found[i].bg;
  const uint16 *bg;
  if (g_own_room >= 0) {
    bg = room == g_own_room ? kInVram : NULL;
  } else {  // scrolling over to the next room: the tilemaps hold some of each
    bg = room == g_seen_room ? g_seen_bg : NULL;
    if (room == dungeon_room_index && subsubmodule_index >= 2)
      bg = dung_bg2;  // Module07_02_01_LoadNextRoom has built it; dung_bg1 follows
  }
  if (!bg && (RoomBesideRoom(g_like_room, room) ||
              (g_own_room < 0 && RoomBesideRoom(dungeon_room_index, room))))
    bg = BuiltRoomTiles(room, g_like_room);
  if (g_num_rooms_found < countof(g_rooms_found)) {
    g_rooms_found[g_num_rooms_found].room = room;
    g_rooms_found[g_num_rooms_found++].bg = bg;
  }
  return bg;
}

static void SeeRoom(int room) {
  g_seen_room = room;
  memcpy(g_seen_bg, dung_bg2, kRoomTiles * sizeof(uint16));
  memcpy(g_seen_bg + kRoomTiles, dung_bg1, kRoomTiles * sizeof(uint16));
}

// Whether the game's in a room, all of it in RAM and VRAM: Link's in control, or the room's
// changing (doors, shutters, water, pegs...), or he's going up or down a step in it. Not on
// his way to another room: stairs, a fall, a warp, or the scroll over to the next one.
static bool DungeonInRoom(void) {
  static const uint32 kInRoom = 1 << 0x00 | 1 << 0x01 | 1 << 0x03 | 1 << 0x04 | 1 << 0x05 |
      1 << 0x08 | 1 << 0x09 | 1 << 0x0a | 1 << 0x0b | 1 << 0x0c | 1 << 0x0d | 1 << 0x10 |
      1 << 0x14 | 1 << 0x16 | 1 << 0x17 | 1 << 0x18 | 1 << 0x1a;
  return main_module_index == 7 && submodule_index < 32 && (kInRoom >> submodule_index & 1);
}

// Whether the room in RAM is still the one last seen, if a little changed (the stairs Link
// takes are put in front of him): a new one differs in thousands of tiles.
static bool RamHoldsSeenRoom(void) {
  if (g_seen_room == 0xffff)
    return false;
  const uint16 *ram = dung_bg2;  // dung_bg1 follows
  int diffs = 0;
  for (int i = 0; i < 2 * kRoomTiles; i++)
    if (ram[i] != g_seen_bg[i] && ++diffs > 256)
      return false;
  return true;
}

// Whether the tilemaps in VRAM hold the room in RAM, all of it: not part way through
// sending a new one.
static bool VramHoldsRamRoom(void) {
  const Ppu *ppu = g_zenv.ppu;
  for (int layer = 0; layer < 2; layer++) {
    const uint16 *ram = layer == 0 ? dung_bg1 : dung_bg2;
    uint16 base = ppu->bgLayer[layer].tilemapAdr;
    for (int ty = 0; ty < 64; ty++)
      for (int tx = 0; tx < 64; tx++)
        if (ppu->vram[TilemapAdr(base, tx, ty)] != ram[ty * 64 + tx])
          return false;
  }
  return true;
}

// Where the top left of the original's picture is in the dungeon, as BG2 is drawn.
static void DungeonViewOrigin(int *hs, int *vs) {
  const BgLayer *bg2 = &g_zenv.ppu->bgLayer[1];
  *hs = BG2HOFS_copy2 + ScrollDelta(bg2->hScroll, BG2HOFS_copy2);
  *vs = BG2VOFS_copy2 + ScrollDelta(bg2->vScroll, BG2VOFS_copy2);
}

// Works out this frame's g_own_room and g_like_room: false if the rooms next door aren't
// drawn.
static bool DungeonFrameSetup(void) {
  const Ppu *ppu = g_zenv.ppu;
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || ppu->extraLeftRight == 0)
    return false;
  const BgLayer *bg1 = &ppu->bgLayer[0], *bg2 = &ppu->bgLayer[1];
  // both tilemaps the whole room, and BG1 not moved off BG2 (a moving floor)
  if (!TilemapIs64x64(bg1) || !TilemapIs64x64(bg2) ||
      bg1->hScroll != bg2->hScroll || bg1->vScroll != bg2->vScroll)
    return false;
  int own;
  if (main_module_index == 14) {
    if (saved_module_for_menu != 7 || submodule_index == 3)
      return false;  // not indoors, or the dungeon's map
    own = g_seen_room;
  } else if (main_module_index != 7) {
    return false;
  } else if (submodule_index == 2) {
    if (cur_palace_index_x2 == 0xff)
      return false;  // in a cave the tilemaps can hold other caves (below)
    own = -1;
  } else if (DungeonInRoom()) {
    own = dungeon_room_index;
    SeeRoom(own);
  } else {
    // on the way to another room (stairs, a fall, a warp): while VRAM holds all of a room,
    // the last one still or by now the next
    if (!VramHoldsRamRoom())
      return false;
    own = RamHoldsSeenRoom() ? g_seen_room : dungeon_room_index;
  }
  if (own >= 0) {
    int hs, vs;
    DungeonViewOrigin(&hs, &vs);
    if (RoomAt(hs + 128, vs + 112) != own)
      return false;  // the camera's elsewhere
    if (own != g_seen_room)
      SeeRoom(own);
    // What's shown of it: all of it in a palace. Houses and caves are put several to a
    // room, a quadrant each, so there only what the camera goes over, as upstream: its
    // bounds are {a0, b0, a1, b1}, a for a quadrant, b for the whole width or height.
    int x = (hs + 128) & ~511, y = (vs + 112) & ~511;
    g_own_x0 = x, g_own_x1 = x + 512, g_own_y0 = y, g_own_y1 = y + 512;
    if (cur_palace_index_x2 == 0xff) {
      int qx = quadrant_fullsize_x >> 1, qy = quadrant_fullsize_y >> 1;
      g_own_x0 = IntMax(g_own_x0, room_bounds_x.v[qx]);
      g_own_x1 = IntMin(g_own_x1, room_bounds_x.v[qx + 2] + 256);
      g_own_y0 = IntMax(g_own_y0, room_bounds_y.v[qy]);
      g_own_y1 = IntMin(g_own_y1, room_bounds_y.v[qy + 2] + 224);
    }
  }
  if (g_seen_room == 0xffff)
    return false;
  if (hdr_dungeon_dark_with_lantern && TS_copy != 0)
    return false;  // the lantern's light cone (upstream)
  // (A dark room's darkness falls on the rooms next door too, as on the original's picture.)
  g_own_room = own, g_like_room = own >= 0 ? own : g_seen_room;
  if (own < 0)
    g_own_x0 = g_own_x1 = g_own_y0 = g_own_y1 = 0;
  return true;
}

// Whether every room the rectangle [x0, x1) x [y0, y1) touches is to be drawn.
static bool RoomsDrawn(int x0, int x1, int y0, int y1) {
  for (int y = y0 & ~511; y < y1; y += 512) {
    for (int x = x0 & ~511; x < x1; x += 512) {
      const uint16 *bg = RoomTiles(RoomAt(x, y));
      if (!bg)
        return false;
      if (bg == kInVram && (IntMax(x0, x) < g_own_x0 || IntMin(x1, x + 512) > g_own_x1 ||
                            IntMax(y0, y) < g_own_y0 || IntMin(y1, y + 512) > g_own_y1))
        return false;  // past what's shown of it
    }
  }
  return true;
}

// The nearest place below x (above it, for EdgeAbove) where what's drawn can change: the
// edge of a room, or of what's shown of the camera's, lo to hi.
static int EdgeBelow(int x, int lo, int hi) {
  int e = (x - 1) & ~511;
  if (lo < x && lo > e)
    e = lo;
  if (hi < x && hi > e)
    e = hi;
  return e;
}

static int EdgeAbove(int x, int lo, int hi) {
  int e = (x & ~511) + 512;
  if (lo > x && lo < e)
    e = lo;
  if (hi > x && hi < e)
    e = hi;
  return e;
}

bool Widescreen_DungeonSideSpace(int *left, int *right, int *bottom) {
  g_num_rooms_found = 0;
  g_dungeon_drawn = DungeonFrameSetup();
  int extra = g_zenv.ppu->extraLeftRight, hs, vs;
  DungeonViewOrigin(&hs, &vs);
  if (!g_dungeon_drawn) {
    if (enhanced_features0 & kFeatures0_WidescreenVisualFixes) {
      // upstream's extents (by the whole room's camera bounds), kept to the room the camera's
      // in: past it the tilemaps wrap round to its other side, and a camera kept to one
      // quadrant can have bounds going past it
      int rx = (hs + 128) & ~511, ry = (vs + 112) & ~511;
      *left = IntMin(*left, IntMax(hs - rx, 0));
      *right = IntMin(*right, IntMax(rx + 512 - hs - 256, 0));
      *bottom = IntMin(*bottom, IntMax(ry + 512 - vs - 224, 0));
    }
    return false;
  }
  // out from the original's picture, room by room, as far as they're drawn
  int l = 0, r = 0, b = 0;
  while (l < extra) {
    int x1 = hs - l, x0 = IntMax(EdgeBelow(x1, g_own_x0, g_own_x1), hs - extra);
    if (!RoomsDrawn(x0, x1, vs, vs + 224))
      break;
    l = hs - x0;
  }
  while (r < extra) {
    int x0 = hs + 256 + r, x1 = IntMin(EdgeAbove(x0, g_own_x0, g_own_x1), hs + 256 + extra);
    if (!RoomsDrawn(x0, x1, vs, vs + 224))
      break;
    r = x1 - hs - 256;
  }
  while (b < 16) {  // with extend_y
    int y0 = vs + 224 + b, y1 = IntMin(EdgeAbove(y0, g_own_y0, g_own_y1), vs + 240);
    if (!RoomsDrawn(hs - l, hs + 256 + r, y0, y1))
      break;
    b = y1 - vs - 224;
  }
  *left = l, *right = r, *bottom = b;
  return true;
}

static void DungeonDrawNeighbors(void) {
  Ppu *ppu = g_zenv.ppu;
  int hs, vs;
  DungeonViewOrigin(&hs, &vs);
  uint16 base2 = ppu->bgLayer[1].tilemapAdr, base1 = ppu->bgLayer[0].tilemapAdr;
  int tx0 = (hs - ppu->extraLeftCur) >> 3, tx1 = (hs + 256 + ppu->extraRightCur + 7) >> 3;
  int ty0 = vs >> 3, ty1 = (vs + 224 + ppu->extraBottomCur + 7) >> 3;
  for (int tx = tx0; tx < tx1; tx++) {
    int x = tx * 8;
    for (int ty = ty0; ty < ty1; ty++) {
      int y = ty * 8;
      if (x + 8 > hs && x < hs + 256 && y + 8 > vs && y < vs + 224)
        continue;  // in the original's picture, if only partly: the game's own
      const uint16 *bg = RoomTiles(RoomAt(x, y));
      if (bg == NULL || bg == kInVram)
        continue;
      int i = ((y & 511) >> 3) * 64 + ((x & 511) >> 3);
      WriteTile(TilemapAdr(base2, tx, ty), bg[i]);
      WriteTile(TilemapAdr(base1, tx, ty), bg[kRoomTiles + i]);
    }
  }
}

// ---- Around the PPU's drawing of a frame ----

void Widescreen_DrawNeighbors(void) {
  g_saved_count = 0;
  if (Widescreen_OverworldShowsNeighbors())
    OverworldDrawNeighbors();
  else if (g_dungeon_drawn)
    DungeonDrawNeighbors();
  g_dungeon_drawn = false;  // until Widescreen_DungeonSideSpace says so again
}

void Widescreen_Restore(void) {
  while (g_saved_count > 0) {
    g_saved_count--;
    g_zenv.ppu->vram[g_saved_adr[g_saved_count]] = g_saved_val[g_saved_count];
  }
}
