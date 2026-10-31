# Changelog

What Zelda LTTP changes from [snesrev/zelda3](https://github.com/snesrev/zelda3), which it's a
fork of. The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- Widescreen is on: `ExtendedAspectRatio = 16:9` in `zelda3.ini` (upstream's own widescreen,
  off there). `4:3` turns it back off.

### Added

- `play.sh`, which builds the program if its code changed and plays the game, showing any
  build error in a dialog.
