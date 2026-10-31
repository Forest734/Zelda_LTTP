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

- Sharp bilinear scaling in the window, `LinearFiltering = Sharp` in `zelda3.ini`, and on
  (SMB's): the picture is scaled up by the largest whole number that fits and blended only
  for the rest, so in fullscreen or a resized window every dot is the same size and
  scrolling doesn't shimmer. At the window's own size (`WindowScale`) it's as before. SDL
  output only; OpenGL takes it as `0`.
- `play.sh`, which builds the program if its code changed and plays the game, showing any
  build error in a dialog.
