# Changelog

What Zelda LTTP changes from [snesrev/zelda3](https://github.com/snesrev/zelda3), which it's a
fork of. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Widescreen is on: `ExtendedAspectRatio = 16:9` in `zelda3.ini` (upstream's own widescreen,
  off there). `4:3` turns it back off.

### Fixed

- On a display slower than 60 Hz, such as a 3440x1440 monitor at 30 Hz over HDMI, the game
  would run as slowly as the display, half speed at 30 Hz, as each frame waited for the
  display. It now runs 60 frames a second there, drawing those the display can show (SMB's
  fix). At 60 Hz it's as before.
- In widescreen, arrows, the boomerang and the like vanished 7 pixels before the edges of the
  picture, and the flute's bird, carrying Link off, vanished with its shadow 23 pixels before
  the right edge: upstream's widescreen kept them 64 pixels out, where 16:9 shows 71. They
  now go on out of the picture, and enemies come and go a little further out of sight.
- In widescreen, the flute's map of where the bird can go was black at the left (upstream drew
  it as the overworld); it's now drawn across the picture, as the world map is.
- In widescreen, a mosaic (going into the woods, say) drew streaks over the space at the left,
  where it read the size of its blocks from outside its table. It now goes on into it.
- A gamepad with SNES-style buttons, such as the iBuffalo SNES Controller, had A and B, and X
  and Y, swapped: SDL names its buttons by their labels, and `[GamepadMap]` by where they
  sit. SDL now names them by where they sit too (SMAS's fix).

### Added

- In widescreen, the overworld goes on past the edges of the area Link is in: the space at
  the sides shows the areas next to it, as they'll be when he walks in (with what the
  game's events have changed in them), where upstream's widescreen showed black. The areas
  next door are drawn without their enemies, which appear as Link goes in, as on the SNES.
- In widescreen, at the edges of the world the camera now stops at the edge, as in a game made
  for a wide screen, where upstream's widescreen went on into black: the picture shows the
  world up to its edge, with Link off centre and the HUD where it was, and enemies are kept
  active as far as it shows on the other side. It stays put as the spotlight closes on a
  door there, or opens on Link coming out.
- In widescreen, in a dungeon room where the camera keeps to one part of the room, the
  space at the sides shows the rest of the room, where upstream showed black (not in houses
  and caves, several of which share a room).
- In widescreen, a dungeon goes on past the edges of the room Link is in: the space at the
  sides shows the rooms next to it, as they'll be when he walks in (with the doors and
  chests he's opened there), while he's in the room, scrolling to the next, and on stairs
  until the screen fades. Where the room next door has other graphics or colours, or is
  dark, it's still black, as it is in houses and caves except through a door. As on the
  overworld, the rooms next door are drawn without their enemies.
- Sharp bilinear scaling in the window, `LinearFiltering = Sharp` in `zelda3.ini` (SMB's):
  the picture is scaled up by the largest whole number that fits and blended only for the
  rest, so in fullscreen or a resized window every dot is the same size and scrolling
  doesn't shimmer. At the window's own size (`WindowScale`) it's as before. SDL output
  only; OpenGL takes it as `0`.
- Smooth pixels, `LinearFiltering = Smooth` in `zelda3.ini`, and on: the stair steps of the
  pixel art are rounded off into slopes and curves, anti-aliased, by xBR (the pixel-art
  scaler emulators offer as a smoothing filter), at the largest whole multiple of the
  picture that fits the window up to 4x, and blended from there to the window's size. Not
  the world map with `EnhancedMode7`, already drawn at 4x. `Sharp` turns it back off. SDL
  output only; OpenGL takes it as `1`, where a `Shader` can do the same.
- `play.sh`, which builds the program if its code changed and plays the game, showing any
  build error in a dialog, and a Triforce icon for it (`resources/zelda-lttp.png`, from
  upstream's Windows icon), for a launcher in the app menu.
