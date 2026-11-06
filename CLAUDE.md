# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Zelda LTTP is The Legend of Zelda: A Link to the Past in C, to be improved in graphics and
playability, as SMB is for Super Mario World. It's a fork of snesrev/zelda3 (remote
`upstream`, `45a149d`), a reimplementation of the SNES game in C made by hand from its
disassembly, which takes its levels, graphics and music from the user's ROM. snesrev also
wrote smw, which SMB forks, so the two share their SNES code (`snes/`), front end and
tools; SMB's CLAUDE.md is the closest guide. What the fork changes is in CHANGELOG.md; mark
every change to upstream code `// [lttp]`, so `git grep '\[lttp\]'` finds them. Upstream is
MIT (LICENSE.txt).

The ROM is the user's `~/Documents/Roms/snes/Legend of Zelda, The - A Link to the Past (U)
[!].smc`, which is unheadered and has the one SHA-1 `assets/util.py` accepts for the US
game (`6D4F10A8...`). It's copied in as `zelda3.sfc`. (The other `... A Link to the
Past.smc` there has a 512-byte copier header and, without it, a different SHA-1: not
accepted.) Never commit the ROM, or `zelda3_assets.dat`, which holds what's extracted from
it; `.gitignore` keeps both out, along with `saves/*.sav`.

## Commands

```sh
python3 assets/restool.py --extract-from-rom   # zelda3.sfc -> zelda3_assets.dat; make does it if missing
make -j"$(nproc)"                              # ./zelda3; needs sdl2-config
./zelda3                                       # reads zelda3.ini here
./play.sh                                      # builds if anything changed, then plays
```

The asset extractor needs Pillow and PyYAML (both installed system-wide). There are no
SDL2 development packages system-wide: build against the no-root copy heretic uses, as SMB
does:

```sh
R=~/.local/opt/heretic-deps/root/usr; L=$R/lib/x86_64-linux-gnu
PATH=$R/bin:$PATH CFLAGS="-O2 -g -Werror -isystem $R/include/x86_64-linux-gnu" \
  make -j"$(nproc)" LDFLAGS="-L$L -Wl,--disable-new-dtags -Wl,-rpath,$L/sdl2-classic:$L"
```

Setting CFLAGS in the environment replaces upstream's `-O2 -Werror`, hence repeating them.
The RPATH lets `./zelda3` run without `LD_LIBRARY_PATH`. The Makefile doesn't track header
dependencies, so after changing a header, `make clean_obj` first.
`./play.sh` is what the Zelda LTTP entry in the app menu runs
(`~/.local/share/applications/zelda-lttp.desktop`, not in git; `StartupWMClass=zelda-lttp`,
which play.sh sets), in the app grid's Games folder with Heretic, SMB and SMAS (gsettings
`org.gnome.desktop.app-folders`). Its icon, `resources/zelda-lttp.png`, is the 256-pixel
image from upstream's `src/platform/win32/triforce.ico`.

The sources have CRLF line endings, and diffs should keep them: the Edit tool does; Python's
text mode doesn't.

## Status

Set up 2026-10-31: cloned, ROM checked and copied in, `play.sh` written. Auto mode wouldn't
let Claude run the freshly downloaded repo's code (the build and the asset extractor), so
the user ran the first `./play.sh`: upstream as it is built cleanly against heretic's SDL2
with `-Werror`, ~16 s for the code and ~1 s for the assets, and `./zelda3` links SDL2
through its RPATH. Since then, widescreen on in zelda3.ini (16:9, 398x224), `tools/drive.py`,
and the slow-display fix below. drive.py works: a shot ~60 frames after each `ref` shows
every chapter drawn into the extra columns, HUD centred; at the world's edges, and the edge
of a small area (chapter 7's swamp), the sides are black. The pacing fix was checked off
screen with an LD_PRELOAD shim faking a 30 Hz vsync'd display (60 frames a second, 30
drawn; 30 without the fix; 60 Hz unchanged), and the user played it on the Dell at full
speed. The dungeon rooms next door (below) were checked off screen only: walks through the
walls of nine dungeons (the reference saves of chapters 2 to 12 but 5 and 7, which start
outdoors), room-to-room scrolls (at 240 lines too), a
spiral staircase in the Eastern Palace, a dozen house and cave entrances, the middle 256
identical to 4:3, and every room of the 0x140 built by `Dungeon_LoadRoom` without an assert.
The sprite margins (below) were checked off screen against the build before them: arrows
both ways, and the flute's bird taking off (from x 4 on screen too) and landing, go past the
edges at 16:9, and 305 shots of the same at 4:3 are identical. The gamepad fix wasn't tried
with a pad. The world's edges (below) were checked off screen against the build before:
every reference save walked left and right, shots identical but where the picture moves,
and there the old picture moved over but for the HUD; the spotlight into Skull Woods and out
of the Eastern Palace, a walk down the world's left edge through a mosaic fade, the flute
there (bird, map, landing), the menus; 4:3 identical.
The dungeon slide (below) was checked off screen, 2026-11-01: nine dungeons walked through
walls left and right and up and down (the shift follows the camera to a room's edge and
glides through each scroll, never more than 3 a frame but across black screens and once
off the top of the map); against 4:3 with sound off, of 180 shots where both runs were at
the same place, 145 identical below the HUD's rows and the rest differing only in sprites; the
item menu, the dungeon's map (moved back to 0 only once the screen's blanked), the spotlight
out of Desert Palace (iris on Link), a spiral staircase in the Ice Palace, two pits.
Per-line sides in vertical scrolls, checked off screen the same day: nine dungeons walked
up and down through walls (eight of them in three columns), 83 scrolls; in 15, between rooms whose
sides differ, each room's lines kept their own sides and the shift went 4 a frame with no
clamping; against the build before, its black strip down the whole picture (to 62 wide)
gone; 4:3 identical.
Death and falls (below), checked off screen the same day: deaths by the world's left edge
(Skull Woods, +71) and right (Eastern Palace, −29), and in Tower of Hera (+71) and the Ice
Palace (−71), with a fairy and without (letters, menu, a choice), at 240 lines too, and
outdoors where the neighbours are drawn: the picture as the frame before but for sprites,
and after the revival; letters and the menu's fairy by the menu, nothing moving in the
frame of the choice. Falls: module 17 set off at Skull Woods (+71, landing at −71) and a
Wallmaster called in Hera and the Ice Palace: shift held, the new room's before it fades in.
4:3 identical to the build before (739 shots); a widescreen walk through all 13 reference
saves identical to it in game RAM (a crc32 of `g_ram` every 20 frames).
The dungeon's map (below), checked off screen 2026-11-06: opened in nine dungeons (the
reference saves' doors), floors scrolled up and down, closed: the sides all board through
the fade in, the scrolls and the fade out, at 240 lines too (its last line needed BG row
241: line y shows row y + 1); the middle 256 identical to the build before and to 4:3, the
rooms before and after identical to the build before; 4:3 identical to it (117 shots).

## Frame pacing

`main()`'s loop runs one frame per pass and presents it with vsync, so a display slower
than 60 Hz slowed the game to its rate: the user's main display is a Dell C3422WE,
3440x1440 at 29.99 Hz over the laptop's HDMI. SMB's fix, ported: when the window's display
reports under 59 Hz (`DisplayIsSlow`; off screen it reports 0), a frame ending 17 ms or
more behind the clock makes the next one run undrawn (`catch_up`), and over 100 ms behind
is a stall whose time is dropped (upstream: 500 ms). Turbo's undrawn frames skip the timing
altogether, as upstream has it.

## Testing

`tools/drive.py` runs the game under gdb, off screen, from a script of inputs, screenshots
and snapshots (its header says how), as SMB's does. Run it with
`SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy`, or a window opens on the user's desktop
with sound. `ref <n>` loads the reference saves in `saves/ref/`, chapter by chapter
through the game, which reach most places quickly. Upstream's cheats make walks easy to
script: `<frame> gdb call PatchCommand(69)` toggles walking through walls (`'E'`) and
`PatchCommand(119)` fills life (`'w'`); with them Link walks straight across areas, though
through a wall he can also walk into a dungeon, or off the map, where the game shows junk.
Walking through walls is `g_ram[0x37f]`, kept in the save, so a `ref` turns it off; with it
on, stairs and holes do nothing. From the 2nd chapter on, each reference save starts at the
next dungeon's door (hold `U`). To go in at any entrance from the overworld, as
`Overworld_Entrance` does: `gdb set var g_ram[0x10E] = <n>` (`which_entrance`; 1 is Link's
house), then `g_ram[0x10] = 15`, `g_ram[0x10C] = 6`, `g_ram[0x11] = 0`, `g_ram[0xB0] = 0`.
A `#` in a script line starts a comment, so a `gdb printf` can't use `%#x`.
To check a change leaves the original picture alone, run the same script with a config at
`ExtendedAspectRatio = 4:3` and compare its shots with the middle 256 of the wide ones
(enemies near the edges differ: widescreen keeps them active 64 pixels further out).
The world's edges: `ref 7` starts by Skull Woods 49 from the left edge (hold `L` and the
picture's moved the full 71), `ref 1` and `ref 5` by the Eastern Palace and its dark world
twin, 29 from the right. To compare with the build before, `git worktree add` it in the
scratchpad, link in `zelda3_assets.dat`, build, and run the same script there; or, when
only a file or two changed, compile those from `git show HEAD:<file>` in the scratchpad
(the other headers symlinked beside them, not `-I src`, whose `features.h` hides glibc's)
and link them with the tree's other objects. `/tmp` has a per-user quota that old
scratchpads fill; `-pipe` keeps the compiler's temporary files out of it. Compare runs with
`EnableAudio = 0` in the config: the game asks the audio thread whether music is playing
(`ZeldaIsMusicPlaying`, for `APUI00`), so with sound on, runs differ with timing, even the
same build twice with different gdb stops; with it off they repeat exactly. Through walls,
Link meets enemies that widescreen keeps active and 4:3 doesn't, so long walks part ways.
The ROM compare (Settings, below) can't follow a `ref`: under drive.py it fails at once
(frame 23, the emulator's RAM still as at boot), with the build before the dungeon slide
too. It also prints every frame after, enough to fill `/tmp`'s quota: count, don't log it.

## Widescreen

Upstream's (`ExtendedAspectRatio`, at 16:9 the picture is 398 wide, 71 extra a side) draws
the extra space only inside the current overworld area or dungeon room
(`ConfigurePpuSideSpace` in zelda_rtl.c), with black past it, and keeps sprites active 64
pixels further out (`kFeatures0_ExtendScreen64`). Ours, in `src/widescreen.c`:

- **Overworld neighbours.** The game keeps only the current area's map16 in RAM
  (`dung_bg2`) and streams it into BG2's 64x64 tilemap (VRAM 0, the world mod 512). Each
  neighbour's map16 is built by the game's own routines (`Overworld_DecompressAndDrawAll
  Quadrants`, `Overworld_HandleOverlaysAndBombDoors`) on a copy of `g_ram` that's put back,
  cached per area until its `save_ow_event_info` changes; `ZeldaDrawPpuFrame` writes their
  tiles past the area's edges into VRAM just before the PPU draws and restores VRAM after,
  so the game's state never sees it. A tile touching the original 256x224 is never written.
  Note the swamp and the dam (0x7b, 0x3b) lose their event overlay when left (overworld.c,
  `Overworld_LoadGFXAndScreenSize`), so seen from next door they're drawn without it.
- **Dungeon rooms.** A room (supertile) is 512x512, all of it in BG2's and BG1's 64x64
  tilemaps (`dung_bg2`, `dung_bg1`), on a grid 16 wide: room `r` at x `(r & 15) * 512`, y
  `(r >> 4) * 512` (houses and caves, 0x100 on, carry on at y 0x2000), and walking past an
  edge leads to `r ± 1` or `r ± 16`. Since 398 < 512, the tilemaps' wrapped-round columns
  are out of view, and the rooms next door are written there as on the overworld, built by
  `Dungeon_LoadRoom` on a copy of `g_ram` (cached until `save_dung_info` of it or the rooms
  round it changes). Only if `RoomsLookAlike`: the same palette (`pal0`), blockset and BG
  layer setup in the room header, and not lights-out. In houses and caves
  (`cur_palace_index_x2 == 0xff`) the grid means nothing, so only a room through a door
  (both door tables, as `Dungeon_CheckAdjacentRoomsForOpenDoors`), and of the room itself
  only the quadrant the camera's kept to: houses share a supertile, one a quadrant.
  `room_bounds_x/y` are `{a0, b0, a1, b1}`, `a` the camera's bounds in a quadrant, `b` in
  the whole room; `ConfigurePpuSideSpace`'s fallback uses `b` in palaces. While scrolling to
  the next room (submodule 2) the old room is drawn from a copy taken while Link was in it
  and the new one from RAM. On stairs, falls and warps the rooms next door are drawn while
  VRAM's tilemaps equal RAM's and the camera's in that room, which is the old one until the
  load (within 256 changed tiles: the stairs' priority bits) and the new one as it fades in.
  Not drawn: lantern-dark rooms (upstream's no extra space), BG1 scrolled off BG2 (moving
  floors), the dungeon map. Teleport edges (`dung_hdr_travel_destinations[4]`) lead
  elsewhere than the grid shows.
- **The world's edges.** Where the world ends (or a special area, 0x80 on, by itself) less
  than 71 past the original's picture, the picture moves off the edge instead, the camera
  stopping there as in a game made wide (`Widescreen_ViewShift`). `Ppu.viewShift` is how far
  the view is moved right of where the camera puts it (+ at the left edge, − at the right):
  the PPU scrolls BG1 and BG2 that much further (`ppu_runLine`, per line, as HDMA can write
  them), moves sprites and windows that much left (`ppu_evaluateSprites`,
  `PpuWindows_Calc`), and leaves BG3 (HUD, text, menus) where it was. The game's sprite
  checks follow it (`ExtendScreenShift()` beside each `ExtendScreenMargin()`), which is
  needed and not just nicer: OAM's 9-bit x can't tell a sprite 120 left of the 256 from one
  392 right of it. In module 9, 14 and 18 (death) over it, and 15/16 (the spotlight) and 17
  (falling into a hole, before the load) outdoors; not in mode 7 (both maps put the camera
  at 0); new renderer only; 0 with the camera off the world (through walls). A large area
  keeps about `(hs & ~15) - 128` to `+ 512` in the tilemap, so up to 20 columns past what
  upstream draws come from `dung_bg2` (`WorldMap16` while scrolling). Things the game clips
  at the 256 with 8-bit screen x (garnishes, `Ancilla_ReturnIfOutsideBounds`; ancillae hit
  tiles only within it) do so nearer the middle when it's moved.
- **The dungeon slide.** Indoors the same `viewShift` keeps the picture inside what's drawn
  round the room: out from the original's picture, up to 142 a side, what's shown of the
  room and the rooms `RoomBesideRoom` it (`MeasureSides`, with a `RoomView`: no tiles
  built), then `RoomViewRange`: the shifts, lo to hi, that keep all 398 in it, or the middle
  of it if it's narrower; `RangeShift` takes the least. 0 in dark rooms (header bit 0). As
  the game's sprite checks follow it, it's game state, not drawing: `Widescreen_EndFrame`,
  from `ZeldaRunFrame` every frame drawn or not, sets `g_dungeon_shift` (and the range,
  `g_dungeon_lo`/`hi`), which `Widescreen_ViewShift` gives indoors and the draw uses,
  clamped so the 256 never goes past what's drawn. The draw measures the sides in bands down
  the picture, split at rooms' edges (and of what's shown of the camera's, and at line 224),
  and where bands differ gives each line its own (`Ppu.extraPerLine`, `extraLeftLine`,
  `extraRightLine`, which `ppu_runLine` swaps in as it does the shift): a room above or
  below with less drawn at a side doesn't cut this one's short. In a room (and
  menus over it, and 0x14, the pit's walk back) from `dungeon_room_index` and the camera; on
  stairs, falls and warps from the room VRAM holds (as the drawing has it), held while it's
  part way through one. Scrolling to the next quadrant or room (submodule 1 or 2) it goes
  from where it was to where it'll be at the scroll's end, by how far along the camera is:
  direction, start and end are taken as it begins, as the game clears
  `overworld_screen_transition` before it's over. Up or down, both rooms are in the picture
  at once, so `ScrollShift` goes as fast as the camera (4 a frame) into the shifts both
  rooms' ranges allow, while there's little of the new one, and on to where it'll be once
  there's little left of the old; with none they both allow (a dark room), as far along as
  the camera is. Black then shows only past the wall of the room coming in or going out,
  in its few lines, for a few frames. Held through the spotlight on the way out
  (module 15 indoors, now drawn as the room), and death and falls (below); 0 on the
  dungeon's map between its blanks (`overworld_map_state` 1 to 5). `Widescreen_Reset`
  after a load or reset.
- **Death and falls.** Upstream's widescreen blacked the sides in modules 17 and 18, and
  the picture jumped back to the middle where it was moved. Now module 18 (death) is drawn
  as `saved_module_for_menu` (7 or 9), and 17 as 9 outdoors (a hole, before the load) or 7
  indoors (a Wallmaster, and the landing after the load), with the shift as it was: indoors
  `Widescreen_EndFrame` holds it in 18 and in 17 takes it from the room VRAM holds (held
  through the load, the new room's by the fade in). Death's red and black (submodule 4 on,
  BG1 and BG2 off; `Widescreen_DeathBackdrop`) get the full sides, so the backdrop fills
  them, and keep the shift, so Link stays where he fell; the game over letters
  (`GameOverText_Draw`) and the menu's fairy (`GameOver_AnimateChoiceFairy`), at fixed
  screen x, add `ExtendScreenShift()` to stay by BG3's menu, which doesn't move. The frame
  in which a choice is made is drawn with the module already 5 (or 0) and the menu still
  up, so the backdrop and shift are held for one frame after 18 (`g_after_death`). The
  revival fairy fades the picture back in where it was. The iris is the game's, 8-bit, so
  it only ever covers the 256 (as the spotlight's).

Also: `MosaicRun` (ppu.c) gives mosaic blocks left of the 256, where upstream read before
`mosaicModulo` and streaked the extra space in mosaic fades; the flute's map (interface
submodule 10) gets the world map's extents, where upstream drew it as outdoors. The
dungeon's map (submodule 3, `overworld_map_state` 1 to 5) gets all of the sides, where
upstream gave it a room's extents with the camera at 0 (black), and its wooden board goes
on across them (`DungeonMapDrawSides`): the board is BG3, a 32x32 screen of it with the
frames on, blank past it, and each row's edge tile, plain wood, is repeated out to the
sides, written into VRAM for the draw as the neighbours are. Upstream's PPU never draws
BG3 past the 256 (the HUD and the menus would repeat, b70aae8); `Ppu.extraBg3` lets it, for
this only.

Still black (not done): where what's drawn round a dungeon room is narrower than the
picture (a house or cave quadrant by itself), and briefly past a wall in a vertical
scroll (above); dark rooms past the original's picture; the top
corners of the tilted world map; with 240 lines, the 16 past the world's bottom edge; past
the 256 as an iris closes or opens (the spotlight, death), the game's window being 8-bit.

Sprites (`kFeatures0_ExtendScreen64`): upstream keeps them active, and draws ancillae, 64
pixels past either side of the 256, 7 short of 16:9's 71, so arrows and the like vanished
7 pixels inside the edges (`Ancilla_SetOam`, `Ancilla_TerminateIfOffscreen`), and the
flute's bird and its shadow 23 inside the right one (`Ancilla27_Duck`). Ours is
`ExtendScreenMargin()` (features.h): the extra width rounded up to 8, at least 64 (72 at
16:9, 88 at 16:9 with 240 lines, 96 at 18:9), and at least 0x48 where upstream had that.
Carrying Link off, the bird now flies 72 further, which from the far left takes long enough
for its height (a byte, rising faster each frame) to wrap round to the ground: in
widescreen it stops climbing after 89 frames, well above the picture. Followers are drawn
only within the 256 (`SetOam_Follower`), which trailing Link they don't leave.

## Settings (zelda3.ini)

Upstream already has much of what SMB had to add: `ExtendedAspectRatio` (4:3, 16:10, 16:9
or 18:9, with `extend_y` for 240 lines; at 16:9 here), `NewRenderer`, `EnhancedMode7` (a
sharper world map), `NoSpriteLimits`, MSU-1 audio, `LinearFiltering`, GLSL shaders with
`OutputMethod = OpenGL`, and `[Features]`: item switching on L/R and bug fixes. Start by
seeing how far those go before writing code.

`LinearFiltering = Sharp` (ours; SMB's) makes `SdlRenderer_EndDraw` scale sharp
bilinear: nearest into a render target at the largest whole multiple that fits, linear from
there (at `WindowScale`'s own size, identical to nearest); the 4x world map, when larger than
the window, goes down linear.

`LinearFiltering = Smooth` (ours, on; the user asked for smoother pixels on 2026-11-01)
scales the frame with xBR level 2, Hyllian's pixel-art scaler, written for the CPU in
`src/smooth.c` (SDL's renderer runs no shaders): each corner of each dot is cut at 45, 30 or
60 degrees where the diagonals round it show an edge, filled with the colour across it,
anti-aliased by coverage tables made once per scale. The PPU draws into a buffer of ours
(`SdlRenderer_BeginDraw`; a locked texture is write-only), `SdlRenderer_EndDrawSmooth`
scales it at the largest whole multiple that fits, 2 to `kSmoothMaxScale` (4), into a
streaming texture, and SDL scales that linear to the window; frames of another size (the 4x
world map) go the old way, linear. Rows are shared out to up to 4 SDL threads, 8 at a time
from an atomic counter. On this laptop's i5-1035G1 it takes 1.5 to 2.5 ms a frame at 3x or
4x (a single thread, 3 to 5); at 6x it was 3.7, mostly writing the 13 MB, hence the cap. It
was checked under ASan/UBSan and TSan with a harness linking `smooth.c` alone, and in the
window as below. drive.py's shots are the PPU's, not the window's; to see the
window, run drive.py with `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software`, a `WindowSize`
in a `--config` of your own, and `<frame> gdb call (int)setenv("SDL_VIDEO_DUMMY_SAVE_FRAMES",
"1", 1)`: each frame presented after is written as a BMP in the current directory.

Like SMB, zelda3 can run the original machine code beside the C code and compare RAM each
frame (`./zelda3 zelda3.sfc`, at 4:3): the check for changes meant to leave the game as it
was.

Gamepads: `[GamepadMap]` names SDL's buttons, which since
`SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS` is 0 (ours, SMAS's) are by position, A the
bottom one, B right, X left, Y top, so its default is right for any pad. With the hint on,
SDL's mapping for the user's pad (a Rockfire, 0583:2060, in SDL's list as the iBuffalo SNES
Controller) names them by label, which swapped A with B and X with Y. Each pad is opened
once (SDL also reports those in at the start as added) and closed when unplugged.

## Upstreams

`origin` is `git@github-forest734:Forest734/Zelda_LTTP.git` (underscore, though the folder
has a hyphen), and the branch is `main` (upstream's is `master`). Bring in upstream's changes
with `git fetch upstream && git merge upstream/master`; keep changes to upstream code small
and marked, which merges more easily.

## Conventions

- Upstream's style: 2-space indents, braces on the same line, `uint8`/`uint16` types.
- Widescreen and other optional changes go behind their setting, and with it off the game
  must stay exactly the original.
- Graphics and playability first; no multiplayer (as for heretic).
- CHANGELOG.md follows Keep a Changelog; add user-facing changes under [Unreleased].
