/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* CANON-4's controls and screen.
 *   white keys 1-16   the cells: tapped, a note or a rest; held, K1 pitch, K2 velocity, K3 chance, K4 length
 *   black keys 1-4    the head shown (GLO held: mute it)       black keys 5-10  its path: rows, columns, snake,
 *                     diagonal, spiral, random                  black key 11     re-sync the heads now
 *   nothing held      K1 rate, K2 transpose (degrees), K3 offset, K4 length of the head shown; PRESETS its direction
 *   ALGO the head     SELECT the tempo      OCT the head's octave     REC the keys write the cells, one after another
 *   ARP dice the pitches (SEQ: undo)   FX re-sync   LFO mute the head   EDIT its SOUND   GLO setup   HOME the field */

#define ACCENT RGB(230, 120, 220)
enum { PG_FIELD, PG_SOUND, PG_SETUP };
static const uint16_t HEAD_C[NHEAD] = {RGB(255, 160, 90), RGB(110, 210, 160), RGB(110, 170, 255), RGB(230, 120, 220)};

#define SET_MAGIC 0x314E4143u
typedef struct {
    uint32_t magic;
    uint8_t slot, ble_off, rsv[2];
} settings_t;
static settings_t set;

static struct {
    uint8_t page, head, cell, srow, grow;
    int8_t held;                                        /* the cell key held (-1 none) */
    uint8_t held_turned;                                /* a knob turned while it was held (then no toggle) */
    uint8_t rec, rec_at;
    uint8_t dirty, save_wait;
    cell_t undo[NCELL];
    uint8_t undo_ok;
    int16_t key_note[NKEYS];
} U;

static const char *const CARD_TEXT[NB] = {
    "FX: RE-SYNC. The four heads start again from their first cell, together.",
    "SEL held: PRESETS changes the scale, SELECT the root. Every head follows.",
    0,
    "LFO mutes or unmutes the head shown.",
    "EDIT: the head's SOUND (wave, filter, envelope, MIDI channel).",
    "GLO: SETUP. Tempo, key, how often the heads meet again, clock, echo, the project slots. Held + black 1-4: mutes.",
    "HOME: the field and the four heads.",
    "SAVE writes the project to its slot. While playing it saves at the stop.",
    "ARP: DICE. New pitches for the notes of the phrase (the rests stay). SEQ takes it back.",
    "SEQ: UNDO the last dice.",
    "PLAY starts and stops.",
    "REC: the keys write the cells' pitches, one after another from the cell shown. REC again to stop.",
    "OCT-: the head an octave down.",
    "OCT+: the head an octave up.",
#if PLAT_SMK37
    0, 0, 0, 0,
#endif
};

static const char *const HINTS[NB] = {
    [B_GLO] = "BLACK 1-4: MUTE HEADS  TAP: SETUP", [B_SEL] = "PRESETS: SCALE  SELECT: ROOT",
};

static void dirty(void) { U.dirty = 1; }
static head_t *hd(void) { return &P.head[U.head % NHEAD]; }
static cell_t *cel(void) { return &P.cell[U.cell % NCELL]; }
static void settings_save(void) { plat_store_save(OBJ_SETTINGS, &set, sizeof set); }
int canon_ble_off(void) { return set.ble_off; }

static void project_save_now(void)
{
    int r;
    P.bpm10 = T.bpm10;
    r = slot_save(set.slot, PROJ_MAGIC, PROJ_VER, &P, sizeof P);
    if (r == 0)
        U.dirty = 0;
    app_say(r == 0 ? "SAVED" : "SAVE", r == 0 ? "" : "FAILED");
}
static void transport_wait_stop(void)
{
    uint32_t t0 = plat_ms();
    T.want = 2;
    while (T.playing && plat_ms() - t0 < 200u)
        plat_yield();
}

#define CUR_INS (&P.ins[U.head % NHEAD])
static void v_u8(char *o, uint32_t v) { i_text((int32_t)v, o); }
static void ic(uint8_t *f, int32_t d, int32_t lo, int32_t hi) { *f = (uint8_t)clamp((int32_t)*f + d, lo, hi); dirty(); }
static void sv_wave(char *o) { s_cpy(o, WAVE_NM[CUR_INS->wave % NWAVE]); }
static void sv_cut(char *o) { v_u8(o, CUR_INS->cutoff); }
static void sv_res(char *o) { v_u8(o, CUR_INS->reso); }
static void sv_env(char *o) { v_u8(o, CUR_INS->envmod); }
static void sv_att(char *o) { v_u8(o, CUR_INS->attack); }
static void sv_dec(char *o) { v_u8(o, CUR_INS->decay); }
static void sv_sus(char *o) { v_u8(o, CUR_INS->sustain); }
static void sv_rel(char *o) { v_u8(o, CUR_INS->release); }
static void sv_lvl(char *o) { v_u8(o, CUR_INS->level); }
static void sv_echo(char *o) { v_u8(o, CUR_INS->echo); }
static void sv_chan(char *o) { if (CUR_INS->chan) v_u8(o, CUR_INS->chan); else s_cpy(o, "OFF"); }
static void sc_wave(int32_t d) { ic(&CUR_INS->wave, d, 0, NWAVE - 1); }
static void sc_cut(int32_t d) { ic(&CUR_INS->cutoff, d, 0, 127); }
static void sc_res(int32_t d) { ic(&CUR_INS->reso, d, 0, 127); }
static void sc_env(int32_t d) { ic(&CUR_INS->envmod, d, 0, 127); }
static void sc_att(int32_t d) { ic(&CUR_INS->attack, d, 0, 127); }
static void sc_dec(int32_t d) { ic(&CUR_INS->decay, d, 0, 127); }
static void sc_sus(int32_t d) { ic(&CUR_INS->sustain, d, 0, 127); }
static void sc_rel(int32_t d) { ic(&CUR_INS->release, d, 0, 127); }
static void sc_lvl(int32_t d) { ic(&CUR_INS->level, d, 0, 127); }
static void sc_echo(int32_t d) { ic(&CUR_INS->echo, d, 0, 127); }
static void sc_chan(int32_t d) { ic(&CUR_INS->chan, d, 0, 16); }
static const row_def_t SOUND_ROWS[] = {
    {"WAVE", sv_wave, sc_wave, 0},       {"CUTOFF", sv_cut, sc_cut, 0},    {"RESONANCE", sv_res, sc_res, 0},
    {"ENV > FILTER", sv_env, sc_env, 0}, {"ATTACK", sv_att, sc_att, 0},    {"DECAY", sv_dec, sc_dec, 0},
    {"SUSTAIN", sv_sus, sc_sus, 0},      {"RELEASE", sv_rel, sc_rel, 0},   {"LEVEL", sv_lvl, sc_lvl, 0},
    {"ECHO SEND", sv_echo, sc_echo, 0},  {"MIDI CHANNEL", sv_chan, sc_chan, 0},
};
#define NSOUND_ROWS (sizeof SOUND_ROWS / sizeof SOUND_ROWS[0])

static void gv_tempo(char *o) { o[0] = 0; i_cat(o, T.bpm10 / 10); s_cat(o, "."); i_cat(o, T.bpm10 % 10); }
static void gv_root(char *o) { s_cpy(o, ROOT_NM[P.root % 12u]); }
static void gv_scale(char *o) { s_cpy(o, SCALE_NM[P.scale % NSCALE]); }
static void gv_sync(char *o) { if (P.sync_bars) { o[0] = 0; s_cat(o, "EVERY "); i_cat(o, P.sync_bars); s_cat(o, " BARS"); } else s_cpy(o, "FREE"); }
static void gv_clk(char *o) { s_cpy(o, CK_NM[T.clk_mode % NCK]); }
static void gv_clko(char *o) { s_cpy(o, T.clk_send ? "SEND" : "OFF"); }
static void gv_et(char *o) { v_u8(o, P.echo_time); }
static void gv_ef(char *o) { v_u8(o, P.echo_fb); }
static void gv_slot(char *o) { o[0] = 0; i_cat(o, set.slot + 1); s_cat(o, slot_used(set.slot, PROJ_MAGIC) ? "  (SAVED)" : "  (EMPTY)"); }
static void gv_ble(char *o) { s_cpy(o, set.ble_off ? "OFF" : "ON"); }
static void gc_tempo(int32_t d) { T.bpm10 = (uint16_t)clamp((int32_t)T.bpm10 + d * 10, 300, 3000); P.bpm10 = T.bpm10; dirty(); }
static void gc_root(int32_t d) { P.root = (uint8_t)((P.root + 12 + d % 12) % 12); dirty(); }
static void gc_scale(int32_t d) { ic(&P.scale, d, 0, NSCALE - 1); }
static void gc_sync(int32_t d) { ic(&P.sync_bars, d, 0, 16); }
static void gc_clk(int32_t d) { T.clk_mode = (uint8_t)clamp(T.clk_mode + d, 0, NCK - 1); P.clk_mode = T.clk_mode; dirty(); }
static void gc_clko(int32_t d) { T.clk_send = d > 0; P.clk_send = T.clk_send; dirty(); }
static void gc_et(int32_t d) { ic(&P.echo_time, d, 0, 127); project_apply(); }
static void gc_ef(int32_t d) { ic(&P.echo_fb, d, 0, 127); project_apply(); }
static void gc_slot(int32_t d) { set.slot = (uint8_t)clamp(set.slot + d, 0, NSLOTS - 1); settings_save(); }
static void gc_ble(int32_t d) { set.ble_off = d < 0; plat_ble_enable(!set.ble_off); settings_save(); }
static void ga_save(void)
{
    if (T.playing) {
        U.save_wait = 1;
        app_say("SAVES", "AT THE STOP");
    } else {
        project_save_now();
    }
}
static void ga_load(void)
{
    if (T.playing)
        transport_wait_stop();
    if (slot_load(set.slot, PROJ_MAGIC, &P, sizeof P)) {
        project_apply();
        U.dirty = 0;
        app_say("LOADED", "");
    } else {
        app_say("SLOT", "EMPTY");
    }
}
static void ga_new(void)
{
    if (T.playing)
        transport_wait_stop();
    project_new();
    project_apply();
    dirty();
    app_say("NEW", "PROJECT");
}
static const row_def_t SETUP_ROWS[] = {
    {"TEMPO", gv_tempo, gc_tempo, 0},    {"ROOT", gv_root, gc_root, 0},    {"SCALE", gv_scale, gc_scale, 0},
    {"HEADS MEET", gv_sync, gc_sync, 0}, {"CLOCK", gv_clk, gc_clk, 0},     {"CLOCK OUT", gv_clko, gc_clko, 0},
    {"ECHO TIME", gv_et, gc_et, 0},      {"ECHO FEEDBACK", gv_ef, gc_ef, 0}, {"SLOT", gv_slot, gc_slot, 0},
    {"SAVE TO SLOT", 0, 0, ga_save},     {"LOAD SLOT", 0, 0, ga_load},     {"NEW PROJECT", 0, 0, ga_new},
    {"BLUETOOTH", gv_ble, gc_ble, 0},
};
#define NSETUP (sizeof SETUP_ROWS / sizeof SETUP_ROWS[0])

static void audition_cell(uint32_t c)
{
    int32_t n;
    if (T.playing || !P.cell[c].on)
        return;
    n = scale_deg(P.cell[c].deg + hd()->transpose, P.root, P.scale, 48 + 12 * hd()->octave);
    live_note(9, &P.ins[U.head], n, 90, 1);
    live_note(9, &P.ins[U.head], n, 0, 0);
}

static void cell_knob(uint32_t k, int32_t d)
{
    cell_t *c = cel();
    switch (k) {
    case 0: c->deg = (int8_t)clamp(c->deg + d, -7, 21); c->on = 1; audition_cell(U.cell); break;
    case 1: c->vel = (uint8_t)clamp(c->vel + d * 4, 10, 127); break;
    case 2: c->chance = (uint8_t)clamp(c->chance + d * 5, 0, 100); break;
    default: c->gate = (uint8_t)clamp(c->gate + d * 5, 5, 100); break;
    }
    dirty();
}
static void head_knob(uint32_t k, int32_t d)
{
    head_t *h = hd();
    switch (k) {
    case 0: h->rate = (uint8_t)clamp(h->rate + d, 0, NRATE - 1); break;
    case 1: h->transpose = (int8_t)clamp(h->transpose + d, -14, 14); break;
    case 2: h->offset = (uint8_t)((h->offset + NCELL + (uint32_t)(d % (int32_t)NCELL)) % NCELL); break;
    default: h->len = (uint8_t)clamp(h->len + d, 1, NCELL); break;
    }
    dirty();
}

static void rec_write(int32_t note)
{
    int32_t best = 0, bd = 999, d;
    for (d = -7; d <= 21; d++) {
        int32_t x = scale_deg(d, P.root, P.scale, 48 + 12 * hd()->octave) - note;
        if (x < 0) x = -x;
        if (x < bd) {
            bd = x;
            best = d;
        }
    }
    U.cell = U.rec_at;
    cel()->deg = (int8_t)(best - hd()->transpose);
    cel()->on = 1;
    U.rec_at = (uint8_t)((U.rec_at + 1u) % NCELL);
    dirty();
}

static void input(void)
{
    uint32_t k;
    kit_input();
    if (DOWN(B_PLAY))
        T.want = T.playing ? 2 : 1;
    if (DOWN(B_REC)) {
        U.rec = !U.rec;
        U.rec_at = U.cell;
        app_say("REC", U.rec ? "KEYS WRITE THE CELLS" : "OFF");
    }
    if (DOWN(B_HOME)) U.page = PG_FIELD;
    if (DOWN(B_EDIT)) U.page = U.page == PG_SOUND ? PG_FIELD : PG_SOUND;
    if (TAPPED(B_GLO)) U.page = U.page == PG_SETUP ? PG_FIELD : PG_SETUP;
    if (DOWN(B_FX)) {
        C.resync = 1;
        app_say("RE-SYNC", "");
    }
    if (DOWN(B_LFO)) {
        hd()->muted ^= 1u;
        dirty();
    }
    if (DOWN(B_ARP)) {
        uint32_t i;
        for (i = 0; i < NCELL; i++)
            U.undo[i] = P.cell[i];
        U.undo_ok = 1;
        for (i = 0; i < NCELL; i++)
            P.cell[i].deg = (int8_t)((int32_t)(xs32(&C.seed) % 10u) - 1);
        dirty();
        app_say("DICE", "SEQ: UNDO");
    }
    if (DOWN(B_SEQ) && U.undo_ok) {
        uint32_t i;
        for (i = 0; i < NCELL; i++) {
            cell_t t = P.cell[i];
            P.cell[i] = U.undo[i];
            U.undo[i] = t;
        }
        dirty();
        app_say("UNDO", "");
    }
    if (TAPPED(B_SAVE))
        ga_save();
    if (DOWN(B_OCTDN) || DOWN(B_OCTUP)) {
        hd()->octave = (int8_t)clamp(hd()->octave + (DOWN(B_OCTUP) ? 1 : -1), -3, 2);
        dirty();
    }
    if (midi_rec_note >= 0) {
        if (U.rec)
            rec_write(midi_rec_note);
        midi_rec_note = -1;
    }
    if (K.enc[EN_ALGO]) {
        U.head = (uint8_t)clamp(U.head + K.enc[EN_ALGO], 0, NHEAD - 1);
        sel_head = U.head;
    }
    if (U.page == PG_SOUND) {
        kit_list_input(SOUND_ROWS, NSOUND_ROWS, &U.srow);
    } else if (U.page == PG_SETUP) {
        kit_list_input(SETUP_ROWS, NSETUP, &U.grow);
    } else {
        if (HELD(B_SEL) && (K.enc[EN_PRESET] || K.enc[EN_SELECT])) {
            kit_use(B_SEL);
            if (K.enc[EN_PRESET]) gc_scale(K.enc[EN_PRESET]);
            if (K.enc[EN_SELECT]) gc_root(K.enc[EN_SELECT]);
            app_say(ROOT_NM[P.root % 12u], SCALE_NM[P.scale % NSCALE]);
        } else {
            if (K.enc[EN_SELECT])
                gc_tempo(K.enc[EN_SELECT]);
            if (K.enc[EN_PRESET]) {
                hd()->dir = (uint8_t)clamp(hd()->dir + K.enc[EN_PRESET], 0, NHDIR - 1);
                dirty();
            }
        }
        for (k = 0; k < 4u; k++)
            if (K.enc[EN_K1 + k]) {
                if (U.held >= 0) {
                    U.held_turned = 1;
                    cell_knob(k, K.enc[EN_K1 + k]);
                } else {
                    head_knob(k, K.enc[EN_K1 + k]);
                }
            }
    }
    for (k = 0; k < NKEYS; k++) {
        int w = white_of(k), b = black_of(k);
        if (K.kdown >> k & 1u) {
            if (U.rec && w >= 0) {
                int32_t n = 53 + (int32_t)k;
                rec_write(n);
                live_note(9, &P.ins[U.head], n, K.vel[k], 1);
                U.key_note[k] = (int16_t)n;
            } else if (w >= 0) {
                U.cell = (uint8_t)w;
                U.held = (int8_t)w;
                U.held_turned = 0;
            } else if (b >= 0 && b < (int)NHEAD) {
                if (HELD(B_GLO)) {
                    kit_use(B_GLO);
                    P.head[b].muted ^= 1u;
                    dirty();
                } else {
                    U.head = (uint8_t)b;
                    sel_head = U.head;
                }
            } else if (b >= 4 && b < 4 + (int)NPATH) {
                hd()->path = (uint8_t)(b - 4);
                dirty();
            } else if (b == 10) {
                C.resync = 1;
            }
        }
        if (K.kup >> k & 1u) {
            if (U.key_note[k] >= 0) {
                live_note(9, &P.ins[U.head], U.key_note[k], 0, 0);
                U.key_note[k] = -1;
            }
            if (w >= 0 && U.held == w) {                /* a tap (no knob turned): note or rest */
                if (!U.held_turned) {
                    cel()->on ^= 1u;
                    audition_cell(U.cell);
                    dirty();
                }
                U.held = -1;
            }
        }
    }
    kit_cards(CARD_TEXT);
}

/* ---- the screen: the 4 x 4 field, each head where it reads; the heads' list at the right ---- */
static void draw_field(void)
{
    uint32_t c, h;
    char t[8];
    for (c = 0; c < NCELL; c++) {
        int32_t x = 4 + (int32_t)(c % 4u) * 40, y = 2 + (int32_t)(c / 4u) * 40;
        const cell_t *ce = &P.cell[c];
        int sel = c == U.cell;
        cv_rect(x, y, 38, 38, ce->on ? K_PANEL : K_BG);
        box(x, y, 38, 38, sel ? K_FG : ce->on ? K_LINE : K_PANEL);
        if (ce->on) {
            int32_t n = scale_deg(ce->deg, P.root, P.scale, 48);
            note_name(n, t);
            text_c(x + 19, y + 10, &FONT_XS, t, K_FG);
            if (ce->chance < 100u)
                cv_rect(x + 3, y + 33, 32 * ce->chance / 100, 2, K_DIM);
        } else {
            text_c(x + 19, y + 10, &FONT_T, "rest", K_FAINT);
        }
        for (h = 0; h < NHEAD; h++)                     /* the heads reading it now: a corner each */
            if (T.playing && C.h[h].cell == c && !P.head[h].muted) {
                int32_t cx = x + (h & 1u ? 30 : 2), cy = y + (h & 2u ? 30 : 2);
                cv_rect(cx, cy, 6, 6, HEAD_C[h]);
            }
    }
    for (h = 0; h < NHEAD; h++) {                       /* the heads */
        const head_t *hh = &P.head[h];
        int32_t y = 2 + (int32_t)h * 40, x = 168;
        uint16_t col = hh->muted ? K_FAINT : HEAD_C[h];
        char s[24];
        if (h == U.head)
            cv_rect(x - 2, y, 72, 38, K_PANEL);
        cv_rect(x - 2, y, 3, 38, col);
        s[0] = (char)('1' + h);
        s[1] = ' ';
        s[2] = 0;
        s_cat(s, RATE_NM[hh->rate % NRATE]);
        cv_text(x + 4, y, &FONT_XS, s, col);
        s_cpy(s, PATH_NM[hh->path % NPATH]);
        cv_text(x + 4, y + 13, &FONT_T, hh->muted ? "MUTED" : s, K_DIM);
        s_cpy(s, "TR ");
        sgn_cat(s, hh->transpose);
        if (hh->octave) {
            s_cat(s, "  OCT ");
            sgn_cat(s, hh->octave);
        }
        if (hh->dir != HD_FWD)
            s_cat(s, hh->dir == HD_REV ? "  BACK" : "  PING");
        cv_text(x + 4, y + 24, &FONT_T, s, K_DIM);
    }
}

static void draw(void)
{
    static const char *const PGN[] = {"FIELD", "SOUND", "SETUP"};
    kit_header("CANON-4", PGN[U.page], ACCENT, T.playing, U.rec, T.bpm10);
    cv_begin(240, 200, K_BG);
    if (K.card < NB && CARD_TEXT[K.card]) {
        kit_card(K.card, CARD_TEXT[K.card], 200, ACCENT);
    } else if (U.page == PG_SOUND) {
        char t[16] = "HEAD ";
        i_cat(t, U.head + 1);
        s_cat(t, " SOUND");
        cv_text(6, 0, &FONT_T, t, HEAD_C[U.head % NHEAD]);
        kit_list_draw(SOUND_ROWS, NSOUND_ROWS, U.srow, 14, 184, ACCENT);
    } else if (U.page == PG_SETUP) {
        cv_text(6, 0, &FONT_T, "SETUP", ACCENT);
        kit_list_draw(SETUP_ROWS, NSETUP, U.grow, 14, 184, ACCENT);
    } else {
        char t[40];
        draw_field();
        cv_rect(0, 166, 240, 1, K_LINE);
        s_cpy(t, ROOT_NM[P.root % 12u]);
        s_cat(t, " ");
        s_cat(t, SCALE_NM[P.scale % NSCALE]);
        s_cat(t, "   HEAD ");
        i_cat(t, U.head + 1);
        s_cat(t, " ");
        s_cat(t, HDIR_NM[hd()->dir % NHDIR]);
        cv_text(6, 170, &FONT_T, t, K_DIM);
        cv_text(6, 184, &FONT_T, U.rec ? "REC: KEYS WRITE THE CELLS" : "TAP A CELL: NOTE / REST.  HOLD: ITS KNOBS",
                U.rec ? K_REC : K_FAINT);
    }
    cv_blit(0, 22);
    if (U.page != PG_FIELD)
        kit_knobs("VALUE", "", "", "", ACCENT);
    else if (U.held >= 0) {
        char a[16] = "PITCH ", b[12] = "VEL ", c[12] = "CHANCE ", d[12] = "LENGTH ";
        sgn_cat(a, cel()->deg);
        i_cat(b, cel()->vel);
        i_cat(c, cel()->chance);
        i_cat(d, cel()->gate);
        kit_knobs(a, b, c, d, K_FG);
    } else {
        char b[12] = "TR ", c[12] = "OFFSET ", d[12] = "LEN ";
        sgn_cat(b, hd()->transpose);
        i_cat(c, hd()->offset);
        i_cat(d, hd()->len);
        kit_knobs(RATE_NM[hd()->rate % NRATE], b, c, d, HEAD_C[U.head % NHEAD]);
    }
    kit_hint(HINTS, ACCENT);
}

static void lights(void)
{
    uint32_t btn = 0, keys = K.keys, dkeys = 0, i;
    if (T.playing)
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (U.rec)
        btn |= 1u << B_REC;
    if (U.dirty)
        btn |= 1u << B_SAVE;
    if (hd()->muted)
        btn |= 1u << B_LFO;
    if (U.page == PG_SOUND)
        btn |= 1u << B_EDIT;
    for (i = 0; i < NCELL; i++) {                       /* white keys: the notes dim, the head shown's cell lit */
        if (T.playing && C.h[U.head].cell == i)
            keys |= 1u << WKEY[i];
        else if (P.cell[i].on)
            dkeys |= 1u << WKEY[i];
    }
    for (i = 0; i < NHEAD; i++)                         /* black keys: the heads, the path */
        if (i == U.head)
            keys |= 1u << BKEY[i];
        else if (!P.head[i].muted)
            dkeys |= 1u << BKEY[i];
    keys |= 1u << BKEY[4 + hd()->path % NPATH];
    plat_lights(btn, keys, 0, dkeys, 1);
}
