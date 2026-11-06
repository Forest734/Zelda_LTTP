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
// At the world's edges, with nothing past them, the picture is moved off them instead
// (Widescreen_ViewShift): the camera stops at the edge, with the original's picture off
// centre and the HUD where it was, and sprites are kept active, and ancillae drawn, as far
// on the other side as it shows (ExtendScreenShift).
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
//
// Where what's past a room's edge isn't drawn (a room that wouldn't look right, or none),
// the picture is moved away from it, as at the world's edges, if what is drawn is as wide
// as the picture: the camera stops short of the edge. Scrolling over to the next quadrant
// or room, the picture goes from where it was moved to where it's to be there, along with
// the camera. As the game's sprite checks follow it, it's worked out from the game's state
// at the end of each frame, drawn or not (Widescreen_EndFrame). The space at the sides is
// worked out line by line, so a room above or below that has less drawn at a side doesn't
// cut this one's short.
//
// The dungeon's map is a wooden board with two frames on it: the board goes on across the
// sides.
//
// The world map, tilted, shows more of the map in its top lines than its bottom ones, and
// at the top corners, wider than 4:3, goes past the map's sides: the sky round it goes on,
// as it's drawn to.
#include "zelda_rtl.h"
#include "variables.h"
#include "overworld.h"
#include "dungeon.h"
#include "widescreen.h"
#include "snes/ppu.h"

// What's written into VRAM for a frame, to put back once it's drawn, and M7SEL's wrap bit
// (Ppu.m7largeField: not wrapping) if changed.
static uint16 g_saved_adr[8192], g_saved_val[8192];
static int g_saved_count;
static bool g_saved_m7_wrap, g_saved_m7_large_field;

// Indoors, how far the picture's moved right (Widescreen_ViewShift), as worked out at the
// end of the last frame; 0 elsewhere. And how far it could have been, lo to hi, and shown
// no more black.
static int g_dungeon_shift, g_dungeon_lo, g_dungeon_hi;

// Death (module 18): how far the picture's moved, as it was before it, and whether this
// frame's the one after it (Widescreen_DeathBackdrop).
static int g_death_shift;
static bool g_dying, g_after_death;

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

// The dungeon's map, from the room faded out to the map faded out (overworld_map_state 1 to
// 5): VRAM holds the map, BG3 its wooden board and frames, with no scroll.
static bool DungeonMapUp(void) {
  return main_module_index == 14 && submodule_index == 3 && (uint8)(overworld_map_state - 1) < 5;
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

// Whether the game's outdoors, with the extra space drawn: not on the world map.
static bool OverworldDrawn(void) {
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || g_zenv.ppu->extraLeftRight == 0)
    return false;
  // under the menus and death, the module they were opened from (ConfigurePpuSideSpace)
  int mod = main_module_index == 14 || main_module_index == 18 ? saved_module_for_menu : main_module_index;
  if ((mod == 15 || mod == 16 || mod == 17) && !player_is_indoors)
    mod = 9;  // the spotlight closing or opening outdoors, or a fall (ConfigurePpuSideSpace)
  if (mod != 9 || (main_module_index == 14 && submodule_index == 7 && overworld_map_state >= 4))
    return false;  // not outdoors, or the world map
  if (g_zenv.ppu->mode == 7)
    return false;  // a map: the world map, or the flute's
  return TilemapIs64x64(&g_zenv.ppu->bgLayer[1]);  // as the game sets it
}

bool Widescreen_OverworldShowsNeighbors(void) {
  // not the special areas (the Master Sword's grove, Zora's waterfall...), nor with BG2 off
  // (dying, once the picture's gone)
  const Ppu *ppu = g_zenv.ppu;
  return OverworldDrawn() && overworld_screen_index < 0x80 &&
      ((ppu->screenEnabled[0] | ppu->screenEnabled[1]) & 2);
}

int Widescreen_ViewShift(void) {
  const Ppu *ppu = g_zenv.ppu;
  if (!(ppu->renderFlags & kPpuRenderFlags_NewRenderer))
    return 0;
  if (!OverworldDrawn())
    return g_dungeon_shift;  // 0 but indoors
  // what there is to draw: the world, or a special area by itself
  int lo = 0, hi = kWorldSize;
  if (overworld_screen_index >= 0x80)
    lo = ow_scroll_vars0.xstart, hi = ow_scroll_vars0.xend + 256;
  int extra = ppu->extraLeftRight, hs = BG2HOFS_copy2;
  if (hs < lo || hs + 256 > hi || hi - lo < 256 + 2 * extra)
    return 0;  // off it (only through walls), or narrower than the picture
  int left = hs - extra, right = hs + 256 + extra;
  return left < lo ? lo - left : right > hi ? hi - right : 0;
}

static void OverworldDrawNeighbors(void) {
  Ppu *ppu = g_zenv.ppu;
  int extra = ppu->extraLeftRight;
  int hs = BG2HOFS_copy2, vs = BG2VOFS_copy2;
  int view = hs + ppu->viewShift;  // where the picture's moved to, at the world's edges
  // the area: 512 or 1024 pixels square, its camera kept 256x224 inside
  int area_size = ow_scroll_vars0.xend - ow_scroll_vars0.xstart + 256;
  int area_left = ow_scroll_vars0.xstart, area_right = area_left + area_size;
  int area_top = ow_scroll_vars0.ystart, area_bottom = area_top + area_size;
  const uint16 *map8 = GetMap16toMap8Table();
  uint16 base = ppu->bgLayer[1].tilemapAdr;
  // In a large area the game keeps 512 of its 1024 pixels in the tilemap, from 128 or so left
  // of the camera, which a picture moved right can go 20 past. There the area's own map,
  // all of it in RAM while Link's about in it.
  bool area_in_ram = main_module_index == 14 || submodule_index == 0;

  int tx0 = IntMax(view - extra, 0) >> 3, tx1 = IntMin(view + 256 + extra + 7, kWorldSize) >> 3;
  int ty0 = IntMax(vs, 0) >> 3, ty1 = IntMin(vs + 240 + 7, kWorldSize) >> 3;  // 240: extend_y
  for (int tx = tx0; tx < tx1; tx++) {
    int x = tx * 8;
    // past what upstream draws, in a large area
    bool beyond = area_size > 512 && (x + 8 <= hs - extra || x >= hs + 256 + extra);
    for (int ty = ty0; ty < ty1; ty++) {
      int y = ty * 8;
      if (x + 8 > hs && x < hs + 256 && y + 8 > vs && y < vs + 224)
        continue;  // in the original's picture, if only partly: the game's own
      uint16 m16;
      if (x >= area_left && x + 8 <= area_right && y >= area_top && y + 8 <= area_bottom) {
        if (!beyond)
          continue;  // in the area: the game's own
        m16 = area_in_ram ? dung_bg2[((y - area_top) >> 4) * 64 + ((x - area_left) >> 4)] : WorldMap16(x, y);
      } else {
        m16 = WorldMap16(x, y);
      }
      WriteTile(TilemapAdr(base, tx, ty), map8[m16 * 4 + (ty & 1) * 2 + (tx & 1)]);
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

// A room the picture's of, and what's shown of it.
typedef struct RoomView {
  int room;
  int x0, x1, y0, y1;
} RoomView;

// This frame's: whether the rooms next door are drawn, the room whose own tiles are all in
// VRAM (-1 while scrolling over to the next, when it's some of each) and how much of it is
// shown (none while scrolling), and the room they're to look like.
static bool g_dungeon_drawn;
static int g_own_room, g_like_room;
static RoomView g_own;

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

// Within a frame, the rooms looked up so far (out to twice the extra space either side, 6
// at most).
static struct { int room; const uint16 *bg; } g_rooms_found[8];
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

// What's shown of a room with the camera at (hs, vs): false if the camera's elsewhere. All
// of it in a palace. Houses and caves are put several to a room, a quadrant each, so there
// only what the camera goes over, as upstream: its bounds are {a0, b0, a1, b1}, a for a
// quadrant, b for the whole width or height.
static bool GetRoomView(int room, int hs, int vs, RoomView *v) {
  if (RoomAt(hs + 128, vs + 112) != room)
    return false;
  int x = (hs + 128) & ~511, y = (vs + 112) & ~511;
  v->room = room;
  v->x0 = x, v->x1 = x + 512, v->y0 = y, v->y1 = y + 512;
  if (cur_palace_index_x2 == 0xff) {
    int qx = quadrant_fullsize_x >> 1, qy = quadrant_fullsize_y >> 1;
    v->x0 = IntMax(v->x0, room_bounds_x.v[qx]);
    v->x1 = IntMin(v->x1, room_bounds_x.v[qx + 2] + 256);
    v->y0 = IntMax(v->y0, room_bounds_y.v[qy]);
    v->y1 = IntMin(v->y1, room_bounds_y.v[qy + 2] + 224);
  }
  return true;
}

// Works out this frame's g_own_room, g_own and g_like_room: false if the rooms next door
// aren't drawn.
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
  } else if (main_module_index == 7 && submodule_index == 2) {
    if (cur_palace_index_x2 == 0xff)
      return false;  // in a cave the tilemaps can hold other caves (below)
    own = -1;
  } else if (DungeonInRoom()) {
    own = dungeon_room_index;
    SeeRoom(own);
  } else if (main_module_index == 7 || (main_module_index == 15 || main_module_index == 17 ||
                                        main_module_index == 18) && player_is_indoors) {
    // on the way to another room (stairs, a fall, a warp), or out (the spotlight closing),
    // or carried off by a Wallmaster, landing from a hole, or dying: while VRAM holds all of
    // a room, the last one still or by now the next
    if (!VramHoldsRamRoom())
      return false;
    own = RamHoldsSeenRoom() ? g_seen_room : dungeon_room_index;
  } else {
    return false;
  }
  memset(&g_own, 0, sizeof(g_own));
  if (own >= 0) {
    int hs, vs;
    DungeonViewOrigin(&hs, &vs);
    if (!GetRoomView(own, hs, vs, &g_own))
      return false;  // the camera's elsewhere
    if (own != g_seen_room)
      SeeRoom(own);
  }
  if (g_seen_room == 0xffff)
    return false;
  if (hdr_dungeon_dark_with_lantern && TS_copy != 0)
    return false;  // the lantern's light cone (upstream)
  // (A dark room's darkness falls on the rooms next door too, as on the original's picture.)
  g_own_room = own, g_like_room = own >= 0 ? own : g_seen_room;
  return true;
}

// Whether every room the rectangle [x0, x1) x [y0, y1) touches is drawn: as this frame's
// tiles are if v is NULL, else as they'd be with the picture of v's room (what's shown of
// it, and the rooms drawn beside it), nothing built.
static bool RoomsDrawn(const RoomView *v, int x0, int x1, int y0, int y1) {
  const RoomView *own = v ? v : &g_own;
  for (int y = y0 & ~511; y < y1; y += 512) {
    for (int x = x0 & ~511; x < x1; x += 512) {
      int room = RoomAt(x, y);
      bool is_own;
      if (v) {
        if (room < 0 || room != v->room && !RoomBesideRoom(v->room, room))
          return false;
        is_own = room == v->room;
      } else {
        const uint16 *bg = RoomTiles(room);
        if (!bg)
          return false;
        is_own = bg == kInVram;
      }
      if (is_own && (IntMax(x0, x) < own->x0 || IntMin(x1, x + 512) > own->x1 ||
                     IntMax(y0, y) < own->y0 || IntMin(y1, y + 512) > own->y1))
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

// How far past the left and right of the original's picture, its camera at hs, what's
// drawn goes on in the lines of the dungeon from y0 to y1, up to limit: out from it, room by
// room, as RoomsDrawn has it.
static void MeasureSides(const RoomView *v, int hs, int y0, int y1, int limit, int *left, int *right) {
  const RoomView *own = v ? v : &g_own;
  int l = 0, r = 0;
  while (l < limit) {
    int x1 = hs - l, x0 = IntMax(EdgeBelow(x1, own->x0, own->x1), hs - limit);
    if (!RoomsDrawn(v, x0, x1, y0, y1))
      break;
    l = hs - x0;
  }
  while (r < limit) {
    int x0 = hs + 256 + r, x1 = IntMin(EdgeAbove(x0, own->x0, own->x1), hs + 256 + limit);
    if (!RoomsDrawn(v, x0, x1, y0, y1))
      break;
    r = x1 - hs - 256;
  }
  *left = l, *right = r;
}

// How far right the picture of v's room can be moved, lo to hi, the camera at (hs, vs), and
// all of it be in what's drawn; if that's narrower than the picture, just to the middle of
// it. Not at all in a dark room, black past what the lantern lights (or past the original's
// picture: upstream).
static void RoomViewRange(const RoomView *v, int hs, int vs, int *lo, int *hi) {
  int extra = g_zenv.ppu->extraLeftRight, l, r;
  if (GetRoomHeaderPtr(v->room)[0] & 1) {
    *lo = *hi = 0;
    return;
  }
  MeasureSides(v, hs, vs, vs + 224, 2 * extra, &l, &r);
  *lo = extra - l, *hi = r - extra;
  if (*lo > *hi)
    *lo = *hi = (r - l) / 2;
}

// Where it's to be moved to: as little as keeps it in what's drawn.
static int RangeShift(int lo, int hi) {
  return IntMin(IntMax(0, lo), hi);
}

// Scrolling over to the next quadrant or room (DungeonTransition_ScrollRoom), the picture
// goes from where it was as the scroll began to where it's to be once there, as far along
// as the camera is. Up or down, the picture's of both at once, the new coming in at the top
// or bottom, and the old going out, so it goes as fast as the camera to where it suits both
// (if anywhere does) while there's little of the new, and from there to where it's to be
// once there's little left of the old.
static bool g_scrolling;
static int g_scroll_dir, g_scroll_shift0, g_scroll_lo0, g_scroll_hi0, g_scroll_cam0, g_scroll_cam1;

static int ScrollShift(void) {
  if (!g_scrolling) {  // as it begins (the game clears the direction before it's over)
    int dir = overworld_screen_transition & 3;  // down, up, right, left
    int cam = dir >= 2 ? BG2HOFS_copy2 : BG2VOFS_copy2;
    // where the camera stops: the target, mod 512, as it goes 4 at a time
    int target = (&up_down_scroll_target)[dir];
    int d = (dir & 1 ? (cam & 0x1fc) - target : target - (cam & 0x1fc)) & 0x1fc;
    g_scrolling = true, g_scroll_dir = dir;
    g_scroll_shift0 = g_dungeon_shift, g_scroll_lo0 = g_dungeon_lo, g_scroll_hi0 = g_dungeon_hi;
    g_scroll_cam0 = cam, g_scroll_cam1 = dir & 1 ? cam - d : cam + d;
  }
  bool across = g_scroll_dir >= 2;
  int cam = across ? BG2HOFS_copy2 : BG2VOFS_copy2, cam1 = g_scroll_cam1;
  int hs = across ? cam1 : BG2HOFS_copy2, vs = across ? BG2VOFS_copy2 : cam1;
  RoomView v;
  int lo1 = 0, hi1 = 0;
  if (GetRoomView(dungeon_room_index, hs, vs, &v))
    RoomViewRange(&v, hs, vs, &lo1, &hi1);
  int s0 = g_scroll_shift0, s1 = RangeShift(lo1, hi1);
  // how far the camera's to go, and has gone
  int total = cam1 - g_scroll_cam0, done = cam - g_scroll_cam0;
  if (total < 0)
    total = -total, done = -done;
  if (total == 0)
    return s1;
  done = IntMin(IntMax(done, 0), total);
  int s = s0 + (s1 - s0) * done / total;
  if (!across && abs(s1 - s0) <= total) {
    int lo = IntMax(g_scroll_lo0, lo1), hi = IntMin(g_scroll_hi0, hi1);
    if (lo <= hi)
      s = IntMin(IntMax(s, lo), hi);
    // no faster than the camera from s0, and in time to be at s1 at the end
    s = IntMax(s, IntMax(s0 - done, s1 - (total - done)));
    s = IntMin(s, IntMin(s0 + done, s1 + (total - done)));
  }
  return s;
}

void Widescreen_EndFrame(void) {
  const Ppu *ppu = g_zenv.ppu;
  g_after_death = g_dying && main_module_index != 18;
  g_dying = main_module_index == 18;
  if (g_dying)  // indoors, held below; outdoors, the camera stays put
    g_death_shift = Widescreen_ViewShift();
  // under the menus and death, the module they were opened from
  int mod = main_module_index == 14 || main_module_index == 18 ? saved_module_for_menu : main_module_index;
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || ppu->extraLeftRight == 0 ||
      !(ppu->renderFlags & kPpuRenderFlags_NewRenderer) ||
      !(mod == 7 || (mod == 15 || mod == 17) && player_is_indoors)) {
    g_dungeon_shift = g_dungeon_lo = g_dungeon_hi = 0, g_scrolling = false;
    return;
  }
  bool scrolling = main_module_index == 7 && (submodule_index == 1 || submodule_index == 2);
  if (!scrolling)
    g_scrolling = false;
  int hs = BG2HOFS_copy2, vs = BG2VOFS_copy2;
  RoomView v;
  bool viewed = false;  // a room's picture, with its range
  if (main_module_index == 15 || main_module_index == 18) {
    // the spotlight closing on the way out, or dying: the picture stays where it is
  } else if (DungeonMapUp()) {
    g_dungeon_shift = 0;
  } else if (scrolling) {
    g_dungeon_shift = ScrollShift();
  } else if (main_module_index == 14 || DungeonInRoom()) {
    if (main_module_index == 7)
      SeeRoom(dungeon_room_index);  // as drawn or not (DungeonFrameSetup)
    viewed = GetRoomView(dungeon_room_index, hs, vs, &v);
  } else if (VramHoldsRamRoom()) {
    // on the way to another room (stairs, a fall, a warp): the room whose picture VRAM
    // holds, the last one still or by now the next (DungeonFrameSetup)
    DungeonViewOrigin(&hs, &vs);
    viewed = GetRoomView(RamHoldsSeenRoom() ? g_seen_room : dungeon_room_index, hs, vs, &v);
  }
  // else (VRAM part way through taking a room, the camera elsewhere) it stays where it is
  if (viewed) {
    RoomViewRange(&v, hs, vs, &g_dungeon_lo, &g_dungeon_hi);
    g_dungeon_shift = RangeShift(g_dungeon_lo, g_dungeon_hi);
  } else {
    g_dungeon_lo = g_dungeon_hi = g_dungeon_shift;
  }
}

void Widescreen_Reset(void) {
  g_seen_room = 0xffff;
  g_scrolling = false;
  g_dungeon_shift = g_dungeon_lo = g_dungeon_hi = 0;
  g_dying = false;
  Widescreen_EndFrame();
}

bool Widescreen_DeathBackdrop(int *shift) {
  const Ppu *ppu = g_zenv.ppu;
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || !(g_dying || g_after_death))
    return false;
  if ((ppu->screenEnabled[0] | ppu->screenEnabled[1]) & 3)
    return false;  // BG1 or BG2 on: the picture, drawn as the area or room
  *shift = g_death_shift;
  return true;
}

int Widescreen_DungeonSideSpace(int *left, int *right, int *bottom) {
  g_num_rooms_found = 0;
  g_dungeon_drawn = DungeonFrameSetup();
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes))
    return 0;
  Ppu *ppu = g_zenv.ppu;
  int extra = ppu->extraLeftRight, hs, vs;
  if (DungeonMapUp() && extra != 0) {
    // the dungeon's map: its board goes on across the sides (DungeonMapDrawSides)
    *left = *right = extra, *bottom = 16;
    ppu->extraBg3 = true;
    return 0;
  }
  DungeonViewOrigin(&hs, &vs);
  if (!g_dungeon_drawn) {
    // upstream's extents (by the whole room's camera bounds), kept to the room the camera's
    // in: past it the tilemaps wrap round to its other side, and a camera kept to one
    // quadrant can have bounds going past it
    int rx = (hs + 128) & ~511, ry = (vs + 112) & ~511;
    int l = IntMin(*left, IntMax(hs - rx, 0));
    int r = IntMin(*right, IntMax(rx + 512 - hs - 256, 0));
    // the picture moved (Widescreen_EndFrame), but not so far that any of the 256 isn't drawn
    int s = IntMin(IntMax(g_dungeon_shift, -l), r);
    *left = IntMin(l + s, extra), *right = IntMin(r - s, extra);
    *bottom = IntMin(*bottom, IntMax(ry + 512 - vs - 224, 0));
    return s;
  }
  // What's drawn past the sides, in bands down the picture split where it can change: at
  // the edges of rooms, and of what's shown of the camera's; and at the bottom of the
  // original's 224, on past which (with extend_y) a band is shown only while all of the 256
  // is drawn.
  enum { kMaxBands = 8 };
  int band_y[kMaxBands + 1], band_l[kMaxBands], band_r[kMaxBands], n = 0;
  int least_l = 2 * extra, least_r = 2 * extra;  // of any band in the 224
  for (int y = vs; y < vs + 240 && n < kMaxBands; n++) {
    int y1 = IntMin(EdgeAbove(y, g_own.y0, g_own.y1), y < vs + 224 ? vs + 224 : vs + 240);
    MeasureSides(NULL, hs, y, y1, 2 * extra, &band_l[n], &band_r[n]);
    if (y < vs + 224)
      least_l = IntMin(least_l, band_l[n]), least_r = IntMin(least_r, band_r[n]);
    band_y[n] = y, band_y[n + 1] = y = y1;
  }
  // the picture moved (Widescreen_EndFrame), but not so far that any of the 256 isn't drawn
  int s = IntMin(IntMax(g_dungeon_shift, -least_l), least_r);
  int most_l = 0, most_r = 0, b = 0;
  bool differ = false;
  for (int i = 0; i < n; i++) {
    int y0 = band_y[i], y1 = band_y[i + 1];
    if (y0 >= vs + 224) {
      if (band_l[i] < -s || band_r[i] < s ||
          !RoomsDrawn(NULL, hs + IntMin(s, 0), hs + 256 + IntMax(s, 0), y0, y1))
        break;
      b = y1 - vs - 224;
    }
    int l = IntMin(band_l[i] + s, extra), r = IntMin(band_r[i] - s, extra);
    if (i > 0 && (l != ppu->extraLeftLine[y0 - 1 - vs] || r != ppu->extraRightLine[y0 - 1 - vs]))
      differ = true;
    memset(&ppu->extraLeftLine[y0 - vs], l, y1 - y0);
    memset(&ppu->extraRightLine[y0 - vs], r, y1 - y0);
    most_l = IntMax(most_l, l), most_r = IntMax(most_r, r);
  }
  ppu->extraPerLine = differ;
  *left = most_l, *right = most_r, *bottom = b;
  return s;
}

static void DungeonDrawNeighbors(void) {
  Ppu *ppu = g_zenv.ppu;
  int hs, vs;
  DungeonViewOrigin(&hs, &vs);
  uint16 base2 = ppu->bgLayer[1].tilemapAdr, base1 = ppu->bgLayer[0].tilemapAdr;
  int view = hs + ppu->viewShift;  // where the picture's moved to
  int tx0 = (view - ppu->extraLeftCur) >> 3, tx1 = (view + 256 + ppu->extraRightCur + 7) >> 3;
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

// The dungeon's map is a 256x224 wooden board (BG3, a 32x32 screen of it) with the floors'
// frame and the map's on it; past its sides BG3 is blank. The board goes on: each row's tile
// at the edge, plain wood, repeated out to the picture's sides.
static void DungeonMapDrawSides(void) {
  Ppu *ppu = g_zenv.ppu;
  const BgLayer *bg3 = &ppu->bgLayer[2];
  if (bg3->hScroll != 0 || bg3->vScroll != 0)
    return;
  int tx0 = -((ppu->extraLeftCur + 7) >> 3), tx1 = 32 + ((ppu->extraRightCur + 7) >> 3);
  int ty1 = ((224 + ppu->extraBottomCur) >> 3) + 1;  // line y shows BG row y + 1, as on the SNES
  for (int ty = 0; ty < ty1; ty++) {
    uint16 left = ppu->vram[TilemapAdr(bg3->tilemapAdr, 0, ty)];
    uint16 right = ppu->vram[TilemapAdr(bg3->tilemapAdr, 31, ty)];
    for (int tx = tx0; tx < 0; tx++)
      WriteTile(TilemapAdr(bg3->tilemapAdr, tx, ty), left);
    for (int tx = 32; tx < tx1; tx++)
      WriteTile(TilemapAdr(bg3->tilemapAdr, tx, ty), right);
  }
}

// ---- The world map ----

// Mode 7 is only ever the world map (the X button's, the flute's, the attract sequence's):
// 512x512, its 64x64 tiles at the top left of mode 7's 128x128 tilemap, the rest of which
// the game fills with tile 0xef, junk (WorldMap_FillTilemapWithEF). Tilted, its top lines
// span more of the map than its bottom ones, never past its sides at 4:3, but up to 37
// past them at 16:9 (black: upstream's 4x renderer leaves out x 512 on, and its other one
// draws the junk there and black left of 0). The sky round the map is drawn to go on
// across its edges, the map's left side to its right, so it does: for the draw, the
// tilemap's columns past it hold the map again, and M7SEL wraps (bit 7 clear: the game
// never writes M7SEL, and upstream's PPU starts with it set), so the map repeats every 512
// pixels.
static void WorldMapDrawSides(void) {
  Ppu *ppu = g_zenv.ppu;
  for (int ty = 0; ty < 64; ty++) {
    for (int tx = 0; tx < 64; tx++) {
      uint16 adr = ty * 128 + tx;  // the tilemap's the low bytes, the tiles' pixels the high
      WriteTile(adr + 64, (ppu->vram[adr + 64] & 0xff00) | (ppu->vram[adr] & 0xff));
    }
  }
  g_saved_m7_wrap = true, g_saved_m7_large_field = ppu->m7largeField;
  ppu->m7largeField = false;
}

// ---- Around the PPU's drawing of a frame ----

void Widescreen_DrawNeighbors(void) {
  g_saved_count = 0;
  bool fixes = (enhanced_features0 & kFeatures0_WidescreenVisualFixes) && g_zenv.ppu->extraLeftRight != 0;
  if (Widescreen_OverworldShowsNeighbors())
    OverworldDrawNeighbors();
  else if (g_dungeon_drawn)
    DungeonDrawNeighbors();
  else if (DungeonMapUp() && fixes)
    DungeonMapDrawSides();
  else if (g_zenv.ppu->mode == 7 && fixes)
    WorldMapDrawSides();
  g_dungeon_drawn = false;  // until Widescreen_DungeonSideSpace says so again
}

void Widescreen_Restore(void) {
  while (g_saved_count > 0) {
    g_saved_count--;
    g_zenv.ppu->vram[g_saved_adr[g_saved_count]] = g_saved_val[g_saved_count];
  }
  if (g_saved_m7_wrap)
    g_zenv.ppu->m7largeField = g_saved_m7_large_field, g_saved_m7_wrap = false;
}
