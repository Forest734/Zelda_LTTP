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

### Added

- In widescreen, the overworld goes on past the edges of the area Link is in: the space at
  the sides shows the areas next to it, as they'll be when he walks in (with what the
  game's events have changed in them), where upstream's widescreen showed black. Only the
  edges of the world itself are still black. The areas next door are drawn without their
  enemies, which appear as Link goes in, as on the SNES.
- In widescreen, in a dungeon room where the camera keeps to one part of the room, the
  space at the sides shows the rest of the room, where upstream showed black (not in houses
  and caves, several of which share a room).
- In widescreen, a dungeon goes on past the edges of the room Link is in: the space at the
  sides shows the rooms next to it, as they'll be when he walks in (with the doors and
  chests he's opened there), while he's in the room, scrolling to the next, and on stairs
  until the screen fades. Where the room next door has other graphics or colours, or is
  dark, it's still black, as it is in houses and caves except through a door. As on the
  overworld, the rooms next door are drawn without their enemies.
- Sharp bilinear scaling in the window, `LinearFiltering = Sharp` in `zelda3.ini`, and on
  (SMB's): the picture is scaled up by the largest whole number that fits and blended only
  for the rest, so in fullscreen or a resized window every dot is the same size and
  scrolling doesn't shimmer. At the window's own size (`WindowScale`) it's as before. SDL
  output only; OpenGL takes it as `0`.
- `play.sh`, which builds the program if its code changed and plays the game, showing any
  build error in a dialog.
