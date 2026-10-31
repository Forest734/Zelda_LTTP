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
speed.

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
through the game, which reach most places quickly.

## Settings (zelda3.ini)

Upstream already has much of what SMB had to add: `ExtendedAspectRatio` (4:3, 16:10, 16:9
or 18:9, with `extend_y` for 240 lines; at 4:3 now), `NewRenderer`, `EnhancedMode7` (a
sharper world map), `NoSpriteLimits`, MSU-1 audio, `LinearFiltering`, GLSL shaders with
`OutputMethod = OpenGL`, and `[Features]`: item switching on L/R and bug fixes. Start by
seeing how far those go before writing code.

Like SMB, zelda3 can run the original machine code beside the C code and compare RAM each
frame (`./zelda3 zelda3.sfc`, at 4:3): the check for changes meant to leave the game as it
was.

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
