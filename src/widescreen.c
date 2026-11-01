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
// at the end of each frame, drawn or not (Widescreen_EndFrame).
#include "zelda_rtl.h"
#include "variables.h"
#include "overworld.h"
#include "dungeon.h"
#include "widescreen.h"
#include "snes/ppu.h"

// What's written into VRAM for a frame, to put back once it's drawn.
static uint16 g_saved_adr[8192], g_saved_val[8192];
static int g_saved_count;

// Indoors, how far the picture's moved right (Widescreen_ViewShift), as worked out at the
// end of the last frame; 0 elsewhere.
static int g_dungeon_shift;

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

// Whether the game's outdoors, with the extra space drawn: not on the world map.
static bool OverworldDrawn(void) {
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || g_zenv.ppu->extraLeftRight == 0)
    return false;
  int mod = main_module_index == 14 ? saved_module_for_menu : main_module_index;
  if ((mod == 15 || mod == 16) && !player_is_indoors)
    mod = 9;  // the spotlight closing or opening outdoors (ConfigurePpuSideSpace)
  if (mod != 9 || (main_module_index == 14 && submodule_index == 7 && overworld_map_state >= 4))
    return false;  // not outdoors, or the world map
  if (g_zenv.ppu->mode == 7)
    return false;  // a map: the world map, or the flute's
  return TilemapIs64x64(&g_zenv.ppu->bgLayer[1]);  // as the game sets it
}

bool Widescreen_OverworldShowsNeighbors(void) {
  // not the special areas (the Master Sword's grove, Zora's waterfall...)
  return OverworldDrawn() && overworld_screen_index < 0x80;
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
  } else if (main_module_index == 7 || main_module_index == 15 && player_is_indoors) {
    // on the way to another room (stairs, a fall, a warp), or out (the spotlight closing):
    // while VRAM holds all of a room, the last one still or by now the next
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

// How far past the left and right of the original's picture at (hs, vs) what's drawn goes
// on, up to limit: out from it, room by room, as RoomsDrawn has it.
static void MeasureSides(const RoomView *v, int hs, int vs, int limit, int *left, int *right) {
  const RoomView *own = v ? v : &g_own;
  int l = 0, r = 0;
  while (l < limit) {
    int x1 = hs - l, x0 = IntMax(EdgeBelow(x1, own->x0, own->x1), hs - limit);
    if (!RoomsDrawn(v, x0, x1, vs, vs + 224))
      break;
    l = hs - x0;
  }
  while (r < limit) {
    int x0 = hs + 256 + r, x1 = IntMin(EdgeAbove(x0, own->x0, own->x1), hs + 256 + limit);
    if (!RoomsDrawn(v, x0, x1, vs, vs + 224))
      break;
    r = x1 - hs - 256;
  }
  *left = l, *right = r;
}

// How far to move the picture right, with l and r drawn past the left and right of the
// original's: as little as keeps all of it in what's drawn, or if that's narrower than the
// picture, to the middle of it.
static int FitShift(int l, int r) {
  int extra = g_zenv.ppu->extraLeftRight, lo = extra - l, hi = r - extra;
  if (lo > hi)
    return (r - l) / 2;
  return lo > 0 ? lo : hi < 0 ? hi : 0;
}

// Where the picture of v's room is to be moved to, the camera at (hs, vs). Not in a dark
// room, black past what the lantern lights (or past the original's picture: upstream).
static int RoomViewShift(const RoomView *v, int hs, int vs) {
  if (GetRoomHeaderPtr(v->room)[0] & 1)
    return 0;
  int l, r;
  MeasureSides(v, hs, vs, 2 * g_zenv.ppu->extraLeftRight, &l, &r);
  return FitShift(l, r);
}

// Scrolling over to the next quadrant or room (DungeonTransition_ScrollRoom), the picture
// goes from where it was as the scroll began to where it's to be once there, as far along
// as the camera is.
static bool g_scrolling;
static int g_scroll_dir, g_scroll_shift0, g_scroll_cam0, g_scroll_cam1;

static int ScrollShift(void) {
  if (!g_scrolling) {  // as it begins (the game clears the direction before it's over)
    int dir = overworld_screen_transition & 3;  // down, up, right, left
    int cam = dir >= 2 ? BG2HOFS_copy2 : BG2VOFS_copy2;
    // where the camera stops: the target, mod 512, as it goes 4 at a time
    int target = (&up_down_scroll_target)[dir];
    int d = (dir & 1 ? (cam & 0x1fc) - target : target - (cam & 0x1fc)) & 0x1fc;
    g_scrolling = true, g_scroll_dir = dir;
    g_scroll_shift0 = g_dungeon_shift, g_scroll_cam0 = cam, g_scroll_cam1 = dir & 1 ? cam - d : cam + d;
  }
  bool across = g_scroll_dir >= 2;
  int cam = across ? BG2HOFS_copy2 : BG2VOFS_copy2, cam1 = g_scroll_cam1;
  int hs = across ? cam1 : BG2HOFS_copy2, vs = across ? BG2VOFS_copy2 : cam1;
  RoomView v;
  int s1 = GetRoomView(dungeon_room_index, hs, vs, &v) ? RoomViewShift(&v, hs, vs) : 0;
  int total = cam1 - g_scroll_cam0, done = cam - g_scroll_cam0;
  if (total == 0)
    return s1;
  done = total > 0 ? IntMin(IntMax(done, 0), total) : IntMax(IntMin(done, 0), total);
  return g_scroll_shift0 + (s1 - g_scroll_shift0) * done / total;
}

void Widescreen_EndFrame(void) {
  const Ppu *ppu = g_zenv.ppu;
  int mod = main_module_index == 14 ? saved_module_for_menu : main_module_index;
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes) || ppu->extraLeftRight == 0 ||
      !(ppu->renderFlags & kPpuRenderFlags_NewRenderer) ||
      !(mod == 7 || mod == 15 && player_is_indoors)) {
    g_dungeon_shift = 0, g_scrolling = false;
    return;
  }
  bool scrolling = main_module_index == 7 && (submodule_index == 1 || submodule_index == 2);
  if (!scrolling)
    g_scrolling = false;
  int hs = BG2HOFS_copy2, vs = BG2VOFS_copy2;
  RoomView v;
  if (main_module_index == 15) {
    // the spotlight closing on the way out: the picture stays where it is
  } else if (main_module_index == 14 && submodule_index == 3 && (uint8)(overworld_map_state - 1) < 5) {
    g_dungeon_shift = 0;  // the dungeon's map, from the room faded out to the map faded out
  } else if (scrolling) {
    g_dungeon_shift = ScrollShift();
  } else if (main_module_index == 14 || DungeonInRoom()) {
    if (main_module_index == 7)
      SeeRoom(dungeon_room_index);  // as drawn or not (DungeonFrameSetup)
    if (GetRoomView(dungeon_room_index, hs, vs, &v))
      g_dungeon_shift = RoomViewShift(&v, hs, vs);
  } else if (VramHoldsRamRoom()) {
    // on the way to another room (stairs, a fall, a warp): the room whose picture VRAM
    // holds, the last one still or by now the next (DungeonFrameSetup)
    DungeonViewOrigin(&hs, &vs);
    if (GetRoomView(RamHoldsSeenRoom() ? g_seen_room : dungeon_room_index, hs, vs, &v))
      g_dungeon_shift = RoomViewShift(&v, hs, vs);
  }
  // else (VRAM part way through taking a room, the camera elsewhere) it stays where it is
}

void Widescreen_Reset(void) {
  g_seen_room = 0xffff;
  g_scrolling = false;
  g_dungeon_shift = 0;
  Widescreen_EndFrame();
}

int Widescreen_DungeonSideSpace(int *left, int *right, int *bottom) {
  g_num_rooms_found = 0;
  g_dungeon_drawn = DungeonFrameSetup();
  if (!(enhanced_features0 & kFeatures0_WidescreenVisualFixes))
    return 0;
  int extra = g_zenv.ppu->extraLeftRight, hs, vs, l, r, b = 0;
  DungeonViewOrigin(&hs, &vs);
  if (g_dungeon_drawn) {
    MeasureSides(NULL, hs, vs, 2 * extra, &l, &r);
  } else {
    // upstream's extents (by the whole room's camera bounds), kept to the room the camera's
    // in: past it the tilemaps wrap round to its other side, and a camera kept to one
    // quadrant can have bounds going past it
    int rx = (hs + 128) & ~511, ry = (vs + 112) & ~511;
    l = IntMin(*left, IntMax(hs - rx, 0));
    r = IntMin(*right, IntMax(rx + 512 - hs - 256, 0));
    b = IntMin(*bottom, IntMax(ry + 512 - vs - 224, 0));
  }
  // the picture moved (Widescreen_EndFrame), but not so far that any of the 256 isn't drawn
  int s = IntMin(IntMax(g_dungeon_shift, -l), r);
  l = IntMin(l + s, extra), r = IntMin(r - s, extra);
  while (g_dungeon_drawn && b < 16) {  // with extend_y
    int y0 = vs + 224 + b, y1 = IntMin(EdgeAbove(y0, g_own.y0, g_own.y1), vs + 240);
    if (!RoomsDrawn(NULL, hs + s - l, hs + s + 256 + r, y0, y1))
      break;
    b = y1 - vs - 224;
  }
  *left = l, *right = r, *bottom = b;
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
