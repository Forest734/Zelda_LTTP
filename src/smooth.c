// [lttp] Smooth pixels (LinearFiltering = Smooth): the frame scaled up by a whole number with
// xBR, Hyllian's scaler for pixel art (level 2, as in his xbr-lv2 shader), written here for
// the CPU, as the SDL renderer runs no shaders.
//
// For each corner of each dot, xBR weighs the differences along the two diagonals round it
// to tell whether an edge crosses the corner, and how steeply. A corner it crosses is cut
// off, and filled with the colour on the other side of the edge: along a 45 degree line,
// or a 30 (shallow, the edge going on along the bottom) or 60 (steep, on up the side), or
// where the edge isn't certain, only the tip of the corner. Each cut is anti-aliased: what
// each of the scale x scale pixels of the dot takes of the other colour is how much of it
// the cut covers, worked out once per scale. The work is shared out by rows to up to 4
// threads.
#include "smooth.h"
#include <string.h>
#include <SDL.h>

#if defined(__GNUC__)
#define SMOOTH_INLINE inline __attribute__((always_inline))
#else
#define SMOOTH_INLINE FORCEINLINE
#endif

enum { kPad = 2 };  // the 5x5 round a dot reaches 2 dots out

// How a corner is cut, for the bottom-right one: x and y run 0 to 1 across the dot, y down.
enum {
  kCut_None,
  kCut_Tip,     // x + y >= 1.75: the edge isn't certain
  kCut_45,      // x + y >= 1.5
  kCut_30,      // x / 2 + y >= 1, and the 45's
  kCut_60,      // x + y / 2 >= 1, and the 45's
  kCut_30And60,
  kCut_Count,
};

typedef struct Yuv { int16 y, u, v, unused; } Yuv;

typedef struct Cut {
  const uint16 *cover;  // scale x scale, of 256
  uint32 color;
  int strength;         // how far color is from the dot's
} Cut;

static int g_scale;
// For each corner (bottom-right, bottom-left, top-right, top-left) and cut, how much of
// each pixel of the dot it covers, of 256.
static uint16 g_cover[4][kCut_Count][kSmoothMaxScale * kSmoothMaxScale];

static int g_w, g_h, g_pw, g_ph;  // g_pw x g_ph: the padded buffers
static int *g_col;          // the frame's column of each padded one
static uint32 *g_px;        // the frame, with its edge dots repeated kPad out round it
static Yuv *g_yuv;
static uint16 *g_down;      // the difference of each dot from the one down to the right (\)
static uint16 *g_up;        // and from the one up to the right (/)

static bool InCut(int cut, float x, float y) {
  switch (cut) {
  case kCut_Tip: return x + y >= 1.75f;
  case kCut_45: return x + y >= 1.5f;
  case kCut_30: return x * 0.5f + y >= 1.0f;
  case kCut_60: return x + y * 0.5f >= 1.0f;
  case kCut_30And60: return x * 0.5f + y >= 1.0f || x + y * 0.5f >= 1.0f;
  }
  return false;
}

static void MakeCovers(int scale) {
  enum { kSamples = 16 };
  memset(g_cover, 0, sizeof(g_cover));
  for (int cut = 1; cut < kCut_Count; cut++) {
    for (int j = 0; j < scale; j++) {
      for (int i = 0; i < scale; i++) {
        int n = 0;
        for (int b = 0; b < kSamples; b++) {
          for (int a = 0; a < kSamples; a++)
            n += InCut(cut, (i + (a + 0.5f) / kSamples) / scale, (j + (b + 0.5f) / kSamples) / scale);
        }
        n = (n * 256 + kSamples * kSamples / 2) / (kSamples * kSamples);
        int fi = scale - 1 - i, fj = scale - 1 - j;
        g_cover[0][cut][j * scale + i] = g_cover[1][cut][j * scale + fi] = (uint16)n;
        g_cover[2][cut][fj * scale + i] = g_cover[3][cut][fj * scale + fi] = (uint16)n;
      }
    }
  }
  g_scale = scale;
}

static bool Alloc(int w, int h) {
  free(g_col), free(g_px), free(g_yuv), free(g_down), free(g_up);
  g_w = w, g_h = h, g_pw = w + 2 * kPad, g_ph = h + 2 * kPad;
  size_t n = (size_t)g_pw * g_ph;
  g_col = malloc(g_pw * sizeof(int));
  g_px = malloc(n * sizeof(uint32));
  g_yuv = malloc(n * sizeof(Yuv));
  g_down = malloc(n * sizeof(uint16));
  g_up = malloc(n * sizeof(uint16));
  if (!g_col || !g_px || !g_yuv || !g_down || !g_up) {
    g_w = g_h = 0;
    return false;
  }
  for (int x = 0; x < g_pw; x++)
    g_col[x] = IntMin(IntMax(x - kPad, 0), w - 1);
  return true;
}

static SMOOTH_INLINE Yuv ToYuv(uint32 v) {
  int r = v >> 16 & 0xff, g = v >> 8 & 0xff, b = v & 0xff;
  Yuv c;
  c.y = (int16)((77 * r + 150 * g + 29 * b) >> 8);
  c.u = (int16)((-43 * r - 85 * g + 128 * b + 32768) >> 8);
  c.v = (int16)((128 * r - 107 * g - 21 * b + 32768) >> 8);
  c.unused = 0;
  return c;
}

static SMOOTH_INLINE int Diff(const Yuv *a, const Yuv *b) {
  return 2 * abs(a->y - b->y) + abs(a->u - b->u) + abs(a->v - b->v);
}

// Close enough in colour to count as one, in the tests for a sure edge.
static SMOOTH_INLINE bool Like(int diff) {
  return diff < 64;
}

// Rows y0 to y1 of the padded buffers, from the frame. The differences to the rows above
// and below are worked out from the frame, so no row waits for another.
static void PrepareRows(const uint32 *src, int src_pitch, int y0, int y1) {
  int h = g_h, pw = g_pw;
  for (int y = y0; y < y1; y++) {
    const uint32 *above = src + IntMin(IntMax(y - 1 - kPad, 0), h - 1) * src_pitch;
    const uint32 *row = src + IntMin(IntMax(y - kPad, 0), h - 1) * src_pitch;
    const uint32 *below = src + IntMin(IntMax(y + 1 - kPad, 0), h - 1) * src_pitch;
    uint32 *px = g_px + y * pw;
    Yuv *yuv = g_yuv + y * pw;
    uint16 *down = g_down + y * pw, *up = g_up + y * pw;
    for (int x = 0; x < pw; x++) {
      int col = g_col[x], next = g_col[IntMin(x + 1, pw - 1)];
      px[x] = row[col] & 0xffffff;
      yuv[x] = ToYuv(px[x]);
      Yuv d = ToYuv(below[next]), u = ToYuv(above[next]);
      down[x] = (uint16)Diff(&yuv[x], &d);
      up[x] = (uint16)Diff(&yuv[x], &u);
    }
  }
}

// The difference between dot p and the one dx, dy from it, |dx| = |dy| = 1.
static SMOOTH_INLINE int DiagDiff(int p, int dx, int dy) {
  if (dx > 0)
    return dy > 0 ? g_down[p] : g_up[p];
  return dy > 0 ? g_up[p - 1 + g_pw] : g_down[p - 1 - g_pw];
}

// The corner of dot e toward sx, sy (each +1 or -1), seen as the bottom-right one: E the
// dot, F right of it, H below, I down to the right, and so on round:
//          A1 B1 C1
//       A0 A  B  C  C4
//       D0 D  E  F  F4
//       G0 G  H  I  I4
//          G5 H5 I5
static SMOOTH_INLINE int CornerCut(int e, int sx, int sy, uint32 *color, int *strength) {
#define P(u, v) (e + (v) * sy * g_pw + (u) * sx)
#define DD(u1, v1, u2, v2) DiagDiff(P(u1, v1), sx * ((u2) - (u1)), sy * ((v2) - (v1)))
  uint32 E = g_px[e], F = g_px[P(1, 0)], H = g_px[P(0, 1)];
  if (E == F || E == H)
    return kCut_None;
  int e_c = DD(0, 0, 1, -1), e_g = DD(0, 0, -1, 1), e_i = DD(0, 0, 1, 1);
  int f_b = DD(1, 0, 0, -1), h_d = DD(0, 1, -1, 0);
  // Along the edge F-H against across it, E-I.
  int along = e_c + e_g + DD(1, 1, 0, 2) + DD(1, 1, 2, 0) + 4 * DD(0, 1, 1, 0);
  int across = h_d + DD(0, 1, 1, 2) + DD(1, 0, 2, 1) + f_b + 4 * e_i;
  if (along > across)
    return kCut_None;
  int e_f = Diff(&g_yuv[e], &g_yuv[P(1, 0)]), e_h = Diff(&g_yuv[e], &g_yuv[P(0, 1)]);
  *color = e_f <= e_h ? F : H;
  *strength = (e_f <= e_h ? e_f : e_h) + 1;
  if (along == across)
    return kCut_Tip;
  bool sure = (!Like(f_b) && !Like(h_d)) ||
              (Like(e_i) && !Like(DD(1, 0, 2, 1)) && !Like(DD(0, 1, 1, 2))) ||
              Like(e_g) || Like(e_c);
  if (!sure)
    return kCut_Tip;
  uint32 B = g_px[P(0, -1)], C = g_px[P(1, -1)], D = g_px[P(-1, 0)], G = g_px[P(-1, 1)];
  int f_g = Diff(&g_yuv[P(1, 0)], &g_yuv[P(-1, 1)]);
  int h_c = Diff(&g_yuv[P(0, 1)], &g_yuv[P(1, -1)]);
  bool shallow = 2 * f_g <= h_c && E != G && D != G;
  bool steep = f_g >= 2 * h_c && E != C && B != C;
  return shallow ? (steep ? kCut_30And60 : kCut_30) : steep ? kCut_60 : kCut_45;
#undef DD
#undef P
}

static SMOOTH_INLINE uint32 Mix(uint32 a, uint32 b, uint32 t) {  // t of 256
  return ((((a & 0xff00ff) * (256 - t) + (b & 0xff00ff) * t) >> 8) & 0xff00ff) |
         ((((a & 0xff00) * (256 - t) + (b & 0xff00) * t) >> 8) & 0xff00);
}

// Rows y0 to y1 of the frame, scaled up into dst.
static void ScaleRows(int y0, int y1, uint32 *dst, int dst_pitch) {
  int w = g_w, pw = g_pw, scale = g_scale;
  for (int y = y0; y < y1; y++) {
    for (int x = 0; x < w; x++) {
      int e = (y + kPad) * pw + x + kPad;
      uint32 E = g_px[e];
      uint32 *out = dst + y * scale * dst_pitch + x * scale;
      Cut cuts[4];
      int n = 0, cut;
#define CORNER(k, sx, sy) \
      if ((cut = CornerCut(e, sx, sy, &cuts[n].color, &cuts[n].strength)) != kCut_None) \
        cuts[n++].cover = g_cover[k][cut];
      CORNER(0, 1, 1) CORNER(1, -1, 1) CORNER(2, 1, -1) CORNER(3, -1, -1)
#undef CORNER
      if (n == 0) {
        for (int j = 0; j < scale; j++, out += dst_pitch)
          for (int i = 0; i < scale; i++)
            out[i] = E;
      } else if (n == 1) {
        const uint16 *cover = cuts[0].cover;
        uint32 color = cuts[0].color;
        for (int j = 0; j < scale; j++, out += dst_pitch, cover += scale)
          for (int i = 0; i < scale; i++)
            out[i] = cover[i] ? Mix(E, color, cover[i]) : E;
      } else {
        // Where cuts overlap, the one that changes the dot most, as the shader has it.
        for (int j = 0; j < scale; j++, out += dst_pitch) {
          for (int i = 0; i < scale; i++) {
            int best = 0, t = 0;
            uint32 color = E;
            for (int k = 0; k < n; k++) {
              int cover = cuts[k].cover[j * scale + i];
              if (cover * cuts[k].strength > best)
                best = cover * cuts[k].strength, t = cover, color = cuts[k].color;
            }
            out[i] = t ? Mix(E, color, t) : E;
          }
        }
      }
    }
  }
}

// The threads, part 0 the caller: each takes kRowsAtATime rows at a time till all are done.
enum { kMaxParts = 4, kRowsAtATime = 8 };
static int g_parts;
static SDL_sem *g_go[kMaxParts], *g_done;
static SDL_atomic_t g_next_row;
static struct {
  int phase;  // 0 PrepareRows, 1 ScaleRows
  const uint32 *src;
  int src_pitch;
  uint32 *dst;
  int dst_pitch;
} g_job;

static void RunPart(int part) {
  int rows = g_job.phase == 0 ? g_ph : g_h;
  for (;;) {
    int y = SDL_AtomicAdd(&g_next_row, kRowsAtATime);
    if (y >= rows)
      break;
    if (g_job.phase == 0)
      PrepareRows(g_job.src, g_job.src_pitch, y, IntMin(y + kRowsAtATime, rows));
    else
      ScaleRows(y, IntMin(y + kRowsAtATime, rows), g_job.dst, g_job.dst_pitch);
  }
}

static int SDLCALL Worker(void *arg) {
  int part = (int)(intptr_t)arg;
  for (;;) {
    SDL_SemWait(g_go[part]);
    RunPart(part);
    SDL_SemPost(g_done);
  }
  return 0;
}

static void StartThreads(void) {
  int parts = IntMin(IntMax(SDL_GetCPUCount(), 1), kMaxParts);
  g_parts = 1;
  if (parts == 1 || !(g_done = SDL_CreateSemaphore(0)))
    return;
  for (int i = 1; i < parts; i++) {
    SDL_Thread *thread;
    if (!(g_go[i] = SDL_CreateSemaphore(0)) ||
        !(thread = SDL_CreateThread(Worker, "smooth", (void *)(intptr_t)i)))
      break;
    SDL_DetachThread(thread);
    g_parts = i + 1;
  }
}

static void RunPhase(int phase) {
  g_job.phase = phase;
  SDL_AtomicSet(&g_next_row, 0);
  for (int i = 1; i < g_parts; i++)
    SDL_SemPost(g_go[i]);
  RunPart(0);
  for (int i = 1; i < g_parts; i++)
    SDL_SemWait(g_done);
}

void Smooth_Scale(const uint32 *src, int src_pitch, int w, int h,
                  uint32 *dst, int dst_pitch, int scale) {
  if ((w != g_w || h != g_h) && !Alloc(w, h))
    return;
  if (scale != g_scale)
    MakeCovers(scale);
  if (!g_parts)
    StartThreads();
  g_job.src = src, g_job.src_pitch = src_pitch;
  g_job.dst = dst, g_job.dst_pitch = dst_pitch;
  RunPhase(0);
  RunPhase(1);
}
