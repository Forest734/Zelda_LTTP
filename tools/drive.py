# Drives the game under gdb, off screen: feeds the joypad frame by frame,
# takes screenshots, saves and loads snapshots. SMB's tools/drive.py, adapted.
#
#   SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy DRIVE_SCRIPT=run.txt \
#     gdb -q -batch -x tools/drive.py --args ./zelda3 --config test.ini
#
# DRIVE_SCRIPT is a file of lines, by frame (from 0, counted by ZeldaRunFrame):
#   <frame> <buttons>      hold these from this frame on: B Y SEL START U D L R
#                          A X LS RS, or - for none
#   <frame> shot <name>    save the frame drawn next as $DRIVE_OUT/<name>.png
#   <frame> save <slot>    save a snapshot, as Shift+F<slot> does (saves/save<slot>.sav)
#   <frame> load <slot>    load one, as F<slot> does
#   <frame> ref <n>        load reference save n (0 = "Chapter 1 - Zelda's Rescue"
#                          in saves/ref/), as key n+1 does
#   <frame> gdb <command>  run a gdb command
#   <frame> quit
# A # starts a comment. A shot is what the PPU drew, as wide as the screen is
# (four times as large on the Mode 7 world map with EnhancedMode7).
import gdb, os, zlib, struct

BITS = {'B': 0, 'Y': 1, 'SEL': 2, 'START': 3, 'U': 4, 'D': 5, 'L': 6, 'R': 7,
        'A': 8, 'X': 9, 'LS': 10, 'RS': 11}
outdir = os.environ.get('DRIVE_OUT', '.')
events = {}
for line in open(os.environ['DRIVE_SCRIPT']):
    line = line.split('#')[0].strip()
    if line:
        f, rest = line.split(None, 1)
        events.setdefault(int(f), []).append(rest)


def write_png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


pending = []      # what to do once stopped: gdb can't call into the game from stop()
shots = []        # names waiting for the next frame drawn


class FrameBp(gdb.Breakpoint):
    def __init__(self):
        super().__init__('ZeldaRunFrame', internal=True)
        self.frame = 0

    def stop(self):
        want_stop = False
        for ev in events.get(self.frame, []):
            parts = ev.split()
            if parts[0] == 'shot':
                shots.append(parts[1])
            elif parts[0] in ('save', 'load', 'ref', 'gdb', 'quit'):
                pending.append(ev)
                want_stop = True
            else:
                v = 0
                for p in parts:
                    if p != '-':
                        v |= 1 << BITS[p]
                gdb.execute('set var g_input1_state = %d' % v)
        self.frame += 1
        return want_stop


class DrawBp(gdb.Breakpoint):
    # ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags) draws into the
    # locked SDL texture (ARGB8888): stop there, and read it once it returns.
    def stop(self):
        if shots:
            pending.append('drawn')
        return bool(shots)


def take_shot(name):
    frame = gdb.selected_frame()
    buf = int(frame.read_var('pixel_buffer'))
    pitch = int(frame.read_var('pitch'))
    scale = int(gdb.parse_and_eval('PpuGetCurrentRenderScale(g_zenv.ppu, render_flags)'))
    w = int(gdb.parse_and_eval('g_snes_width')) * scale
    h = int(gdb.parse_and_eval('g_snes_height')) * scale
    gdb.execute('finish', to_string=True)
    mem = gdb.selected_inferior()
    rgb = bytearray(w * h * 3)
    for y in range(h):
        row = bytes(mem.read_memory(buf + y * pitch, w * 4))
        o = y * w * 3
        rgb[o:o + w * 3:3], rgb[o + 1:o + w * 3:3], rgb[o + 2:o + w * 3:3] = row[2::4], row[1::4], row[0::4]
    write_png(os.path.join(outdir, name + '.png'), w, h, bytes(rgb))
    print('shot %s: %dx%d' % (name, w, h))


FrameBp()
DrawBp('ZeldaDrawPpuFrame', internal=True)
gdb.execute('run')
done = False
while not done:
    while pending:
        ev = pending.pop(0)
        parts = ev.split()
        if parts[0] == 'drawn':
            take_shot(shots.pop(0))
        elif parts[0] == 'save':
            gdb.execute('call SaveLoadSlot(kSaveLoad_Save, %s)' % parts[1], to_string=True)
        elif parts[0] == 'load':
            gdb.execute('call SaveLoadSlot(kSaveLoad_Load, %s)' % parts[1], to_string=True)
        elif parts[0] == 'ref':
            gdb.execute('call SaveLoadSlot(kSaveLoad_Load, 256 + %s)' % parts[1], to_string=True)
        elif parts[0] == 'gdb':
            gdb.execute(ev[4:])
        elif parts[0] == 'quit':
            done = True
    if not done:
        gdb.execute('continue')
gdb.execute('kill')
