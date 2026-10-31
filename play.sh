#!/usr/bin/env bash
# Plays A Link to the Past, building zelda3 first if the sources changed (SMB's play.sh,
# adapted). Options given are passed on to zelda3, which reads zelda3.ini here. Errors
# show in a dialog too, for when there's no terminal (an app menu launcher).
set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")"

R=~/.local/opt/heretic-deps/root/usr; L=$R/lib/x86_64-linux-gnu
export PATH=$R/bin:$PATH
# in the environment, not on make's command line, which would drop the Makefile's additions
export CFLAGS="-O2 -g -Werror -isystem $R/include/x86_64-linux-gnu"

fail() {
  echo "$1" >&2
  zenity --error --title="Zelda LTTP" --width=640 --no-markup --text="$1" 2>/dev/null
  exit 1
}

[ -f zelda3_assets.dat ] || [ -f zelda3.sfc ] || fail "There's no zelda3.sfc in $PWD: copy in Legend of Zelda, The - A Link to the Past (U) [!].smc from ~/Documents/Roms/snes."

build=(make -j"$(nproc)" LDFLAGS="-L$L -Wl,--disable-new-dtags -Wl,-rpath,$L/sdl2-classic:$L")
if ! "${build[@]}" -q 2>/dev/null; then
  notify-send "Zelda LTTP" "Building the latest code…" 2>/dev/null
  if ! log=$("${build[@]}" 2>&1); then
    # the compiler's errors, not every command line's -Werror
    errors=$(grep -E -B2 -A3 -m 8 'error:|Error [0-9]|Exception' <<<"$log")
    fail "The build failed, so the game wasn't started:"$'\n\n'"${errors:-$(tail -n 25 <<<"$log")}"
  fi
fi

# the window's class, for an app menu entry's StartupWMClass
export SDL_VIDEO_X11_WMCLASS=zelda-lttp SDL_VIDEO_WAYLAND_WMCLASS=zelda-lttp
exec ./zelda3 "$@"
