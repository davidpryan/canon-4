/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* CANON-4: one phrase, four readers. Sixteen CELLS (a 4 x 4 field, a scale degree each, or a rest) are read at the
 * same time by four PLAYHEADS, after Fugue Machine, PolyPhase and Make Noise's Rene: each head has its own RATE,
 * DIRECTION, PATH through the field (rows, columns, a snake, diagonals, a spiral, at random), OFFSET, LENGTH and a
 * TRANSPOSE in scale degrees, and plays its own sound. Four heads on one sixteen-note phrase make a canon: the same
 * tune at a third above, at half speed below, backwards on top. Change one cell and every voice hears it.
 * Heads can run free (they drift apart) or meet again every few bars (RE-SYNC). */

#define NCELL 16u
#define NHEAD 4u
enum { PATH_ROW, PATH_COL, PATH_SNAKE, PATH_DIAG, PATH_SPIRAL, PATH_RAND, NPATH };
static const char *const PATH_NM[NPATH] = {"ROWS", "COLUMNS", "SNAKE", "DIAGONAL", "SPIRAL", "RANDOM"};
static const uint8_t PATHS[NPATH - 1][NCELL] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15},
    {0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12}, {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15},
    {0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9}};
enum { HD_FWD, HD_REV, HD_PING, NHDIR };
static const char *const HDIR_NM[NHDIR] = {"FORWARD", "BACKWARD", "PING-PONG"};
static const uint16_t RATE_T[] = {12, 16, 24, 32, 48, 64, 96, 192};
static const char *const RATE_NM[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2"};
#define NRATE 8u

typedef struct {
    int8_t deg;                                         /* -7..21 */
    uint8_t on, vel, chance;
    uint8_t gate, rsv[3];                               /* gate % of the head's step */
} cell_t;

typedef struct {
    uint8_t rate, dir, path, offset, len;
    int8_t transpose, octave;
    uint8_t muted;
} head_t;

#define PROJ_MAGIC 0x344E4143u                          /* "CAN4" */
#define PROJ_VER 1u
typedef struct {
    uint16_t bpm10;
    uint8_t root, scale, clk_mode, clk_send, echo_time, echo_fb;
    uint8_t sync_bars;                                  /* 0 free; else the heads meet every N bars */
    uint8_t rsv0[3];
    cell_t cell[NCELL];
    head_t head[NHEAD];
    patch_t ins[NHEAD];
    uint8_t rsv[24];
} proj_t;
static proj_t P;

static struct {
    struct {
        int16_t i;                                      /* the index along its path (-1 before the first) */
        int8_t pdir;
        uint8_t cell;                                   /* the cell it reads now */
        uint32_t t;
        int16_t note;
    } h[NHEAD];
    uint32_t seed;
    volatile uint8_t resync;
} C;

static uint32_t head_cell(uint32_t h, uint32_t i)       /* the cell at index i of head h's path */
{
    const head_t *hd = &P.head[h];
    uint32_t k = (i + hd->offset) % NCELL;
    if (hd->path >= PATH_RAND)
        return xs32(&C.seed) % NCELL;
    return PATHS[hd->path % (NPATH - 1u)][k];
}

static void head_step(uint32_t h)
{
    const head_t *hd = &P.head[h];
    uint32_t len = hd->len ? hd->len : 1u, cell;
    int32_t i = C.h[h].i;
    const cell_t *c;
    switch (hd->dir) {
    case HD_REV: i = i <= 0 ? (int32_t)len - 1 : i - 1; break;
    case HD_PING:
        if (!C.h[h].pdir)
            C.h[h].pdir = 1;
        if (i < 0) {
            i = 0;
            break;
        }
        i += C.h[h].pdir;
        if (i >= (int32_t)len || i < 0) {
            C.h[h].pdir = (int8_t)-C.h[h].pdir;
            i = clamp(i + 2 * C.h[h].pdir, 0, (int32_t)len - 1);
        }
        break;
    default: i = i + 1 >= (int32_t)len ? 0 : i + 1; break;
    }
    C.h[h].i = (int16_t)i;
    cell = head_cell(h, (uint32_t)i);
    C.h[h].cell = (uint8_t)cell;
    c = &P.cell[cell];
    if (!c->on || hd->muted)
        return;
    if (c->chance < 100u && xs32(&C.seed) % 100u >= c->chance)
        return;
    {
        int32_t n = scale_deg(c->deg + hd->transpose, P.root, P.scale, 48 + 12 * hd->octave);
        play_note(h, &P.ins[h], n, c->vel, ticks_to_samples(RATE_T[hd->rate % NRATE] * (uint32_t)clamp(c->gate, 5, 100) / 100u + 1u), 1);
        C.h[h].note = (int16_t)n;
    }
}

static void heads_reset(void)
{
    uint32_t h;
    for (h = 0; h < NHEAD; h++) {
        C.h[h].i = P.head[h].dir == HD_REV ? 0 : -1;
        C.h[h].pdir = 1;
        C.h[h].t = 0;
    }
}

static void seq_tick(void)
{
    uint32_t h;
    if (C.resync || (P.sync_bars && T.tick % (384u * P.sync_bars) == 0u && T.tick)) {
        C.resync = 0;
        heads_reset();
    }
    for (h = 0; h < NHEAD; h++) {
        if (C.h[h].t == 0u)
            head_step(h);
        if (++C.h[h].t >= RATE_T[P.head[h].rate % NRATE])
            C.h[h].t = 0;
    }
}

static void seq_started(void)
{
    uint32_t h;
    heads_reset();
    for (h = 0; h < NHEAD; h++)
        C.h[h].note = -1;
}
static void seq_stopped(void)
{
    uint32_t h;
    for (h = 0; h < NHEAD; h++)
        C.h[h].note = -1;
}

static volatile int16_t midi_rec_note = -1;
static volatile uint8_t sel_head;
static void seq_midi_note(int32_t note, uint32_t vel, uint32_t chan)
{
    (void)chan;
    if (vel) {
        play_note(9, &P.ins[sel_head % NHEAD], note, vel, 0, 0);
        midi_rec_note = (int16_t)note;
    } else {
        note_end(9, &P.ins[sel_head % NHEAD], note);
    }
}

static void project_new(void)
{
    /* a sixteen-note subject, then four heads: as written, a third above at half speed, backwards an octave up on
     * a snake, and a slow bass on the columns */
    static const int8_t SUBJ[NCELL] = {0, 2, 4, 3, 5, 4, 2, 1, 0, 4, 7, 6, 4, 2, 3, 1};
    static const uint8_t ON[NCELL] = {1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1};
    uint32_t i;
    uint8_t *b = (uint8_t *)&P;
    for (i = 0; i < sizeof P; i++)
        b[i] = 0;
    P.bpm10 = 1000;
    P.root = 7;                                         /* G minor */
    P.scale = SC_MINOR;
    P.clk_mode = CK_INTERNAL;
    P.clk_send = 1;
    P.echo_time = 100;
    P.echo_fb = 65;
    P.sync_bars = 4;
    for (i = 0; i < NCELL; i++) {
        P.cell[i].deg = SUBJ[i];
        P.cell[i].on = ON[i];
        P.cell[i].vel = i % 4u ? 88 : 112;
        P.cell[i].chance = 100;
        P.cell[i].gate = 70;
    }
    for (i = 0; i < NHEAD; i++) {
        head_t *h = &P.head[i];
        h->rate = 4;                                    /* eighths */
        h->dir = HD_FWD;
        h->path = PATH_ROW;
        h->offset = 0;
        h->len = NCELL;
        h->transpose = 0;
        h->octave = 0;
        h->muted = 0;
        patch_default(&P.ins[i], PT_SYNTH, i + 1u);
    }
    P.head[1].rate = 6;                                 /* quarters, a third up */
    P.head[1].transpose = 2;
    P.head[2].rate = 2;                                 /* sixteenths, backwards on the snake, an octave up */
    P.head[2].dir = HD_REV;
    P.head[2].path = PATH_SNAKE;
    P.head[2].octave = 1;
    P.head[2].len = 12;
    P.head[3].rate = 7;                                 /* half notes on the columns, two octaves down */
    P.head[3].path = PATH_COL;
    P.head[3].octave = -2;
    P.ins[0].wave = WV_TRI;
    P.ins[0].decay = 60;
    P.ins[0].sustain = 40;
    P.ins[0].echo = 50;
    P.ins[1].wave = WV_SQR;
    P.ins[1].cutoff = 55;
    P.ins[1].level = 75;
    P.ins[1].echo = 60;
    P.ins[2].wave = WV_SINE;
    P.ins[2].decay = 30;
    P.ins[2].sustain = 0;
    P.ins[2].echo = 90;
    P.ins[2].level = 80;
    P.ins[3].wave = WV_SAW;
    P.ins[3].cutoff = 35;
    P.ins[3].reso = 60;
    P.ins[3].attack = 20;
    P.ins[3].sustain = 90;
    P.ins[3].release = 60;
}

static void project_apply(void)
{
    T.bpm10 = P.bpm10;
    T.clk_mode = P.clk_mode;
    T.clk_send = P.clk_send;
    echo_time = 0.05f + (float)P.echo_time * (0.32f / 127.0f);
    echo_fb = (float)P.echo_fb * (0.85f / 127.0f);
}
