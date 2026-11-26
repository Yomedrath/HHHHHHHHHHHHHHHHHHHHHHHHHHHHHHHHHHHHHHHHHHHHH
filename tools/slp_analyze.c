/* slp_analyze – sucht Bewegungstechniken in einem Slippi-Replay und spielt jede Szene mit
 * denselben Controller-Eingaben in unserer Engine nach. Vergleicht Frame für Frame.
 *
 *   slp_analyze replay.slp [--csv frames.csv] [--top N]
 *
 * Techniken: Wavedash, Waveland, Dashdance, Short Hop, Full Hop. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "slp.h"

typedef enum { T_WAVEDASH, T_WAVELAND, T_DASHDANCE, T_SHORTHOP, T_FULLHOP, T_COUNT } Tech;
static const char *TECH_NAMES[T_COUNT] = { "Wavedash", "Waveland", "Dashdance", "Short Hop", "Full Hop" };

typedef struct {
    Tech tech;
    int port, start, end;           /* Frame-Indizes (0 = Frame -123), inklusive */
    float real_dx, eng_dx;          /* Weg in x über die Szene */
    float real_h, eng_h;            /* Sprunghöhe (nur Hops) */
    float max_err, end_err;         /* Positionsabweichung */
    int frames, state_match;        /* Frames mit gleichem Zustand (Eingabe gleich interpretiert?) */
} Event;

typedef struct { Event *v; int n, cap; } EventList;

static void push(EventList *l, Event e)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        Event *nv = realloc(l->v, (size_t)cap * sizeof(Event));
        if (!nv) return;
        l->v = nv;
        l->cap = cap;
    }
    l->v[l->n++] = e;
}

/* ---------- Zuordnung Melee -> Engine ---------- */

static int map_char(int ext)
{
    switch (ext) {
    case 2: return CHAR_FUCHS;
    case 0: return CHAR_BIRDIE;
    case 19: return CHAR_ASSASSIN;
    case 9: return CHAR_DUELIST;
    default: return -1;
    }
}

static int map_stage(int s)
{
    switch (s) {
    case 31: return 0;  /* BF */
    case 32: return 1;  /* FD */
    case 8: return 2;   /* YS */
    case 28: return 3;  /* DL */
    case 2: return 4;   /* FoD */
    case 3: return 5;   /* PS */
    default: return -1;
    }
}

static FState map_action(uint16_t a, bool airborne)
{
    switch (a) {
    case AS_DASH: return ST_DASH;
    case AS_RUN: return ST_RUN;
    case AS_TURN: case AS_TURN_RUN: return ST_TURN;
    case AS_WALK_SLOW: case AS_WALK_MID: case AS_WALK_FAST: return ST_WALK;
    case AS_KNEE_BEND: return ST_JUMPSQUAT;
    case AS_SQUAT: case AS_SQUAT_WAIT: return ST_CROUCH;
    case AS_ESCAPE_AIR: return ST_AIRDODGE;
    case AS_FALL_SPECIAL: return ST_FREEFALL;
    default: return airborne ? ST_AIR : ST_IDLE;
    }
}

/* grobe Zustandsgruppen, damit Melee- und Engine-Zustände vergleichbar sind */
static int group_of(FState s)
{
    switch (s) {
    case ST_DASH: return 1;
    case ST_RUN: return 2;
    case ST_TURN: return 3;
    case ST_JUMPSQUAT: return 4;
    case ST_AIRDODGE: return 5;
    case ST_LANDING: return 6;
    case ST_AIR: case ST_FREEFALL: return 7;
    default: return 0;   /* Idle, Walk, Crouch ... */
    }
}

static int real_group(const SlpFrame *f)
{
    if (f->action == AS_LANDING_FALL_SPECIAL || f->action == AS_LANDING) return 6;
    return group_of(map_action(f->action, f->airborne));
}

static Input map_input(const SlpFrame *s)
{
    Input in;
    memset(&in, 0, sizeof(in));
    in.stick_x = s->stick_x;
    in.stick_y = s->stick_y;
    in.cstick_x = s->cstick_x;
    in.cstick_y = s->cstick_y;
    in.a = (s->buttons & SLP_BTN_A) != 0;
    in.b = (s->buttons & SLP_BTN_B) != 0;
    in.jump = (s->buttons & (SLP_BTN_X | SLP_BTN_Y)) != 0;
    in.l = (s->buttons & SLP_BTN_L) != 0;
    in.r = (s->buttons & SLP_BTN_R) != 0;
    in.z = (s->buttons & SLP_BTN_Z) != 0;
    in.start = (s->buttons & SLP_BTN_START) != 0;
    return in;
}

/* ---------- Szene in der Engine nachspielen ---------- */

static FILE *csv = NULL;
static int event_id = 0;

static void simulate(const SlpGame *sg, Event *ev)
{
    const SlpFrame *fr = sg->frames[ev->port];
    Game g;
    CharId c = (CharId)map_char(sg->ext_char[ev->port]);
    CharId cs[2] = { c, c };
    game_init(&g, map_stage(sg->stage), cs);
    Fighter *f = &g.f[0];
    Fighter *other = &g.f[1];
    other->state = ST_DEAD;                     /* Gegner aus dem Weg */
    other->pos.x = 1e4f;

    /* Startzustand = echter Zustand nach dem Frame vor der Szene */
    const SlpFrame *s0 = &fr[ev->start - 1];
    f->pos.x = s0->x;
    f->pos.y = s0->y;
    f->facing = s0->facing < 0 ? -1 : 1;
    f->grounded = !s0->airborne;
    f->surface = -1;
    if (f->grounded) {
        float best = 1e9f;
        for (int i = 0; i < g.stage->nsurf; i++) {
            const Surface *su = &g.stage->surf[i];
            float d = fabsf(su->y - s0->y);
            if (f->pos.x >= su->x0 - 1 && f->pos.x <= su->x1 + 1 && d < best) { best = d; f->surface = i; }
        }
        if (f->surface < 0) f->grounded = false;
        else f->pos.y = g.stage->surf[f->surface].y;
    }
    f->state = map_action(s0->action, !f->grounded);
    f->state_frame = (int)s0->action_frame - 1;
    if (f->state_frame < 0) f->state_frame = 0;
    f->vel.x = f->grounded ? s0->ground_x : s0->self_air_x;
    f->vel.y = f->grounded ? 0 : s0->self_y;
    if (sg->version[0] < 3 || (sg->version[0] == 3 && sg->version[1] < 5)) {
        /* vor Slippi 3.5 keine Geschwindigkeiten im Replay -> aus Positionsänderung schätzen */
        f->vel.x = s0->x - fr[ev->start - 2].x;
        f->vel.y = f->grounded ? 0 : s0->y - fr[ev->start - 2].y;
    }
    f->jumps_left = s0->jumps_left;
    f->in = map_input(s0);
    f->stick_x_hist[0] = s0->stick_x;
    f->stick_x_hist[1] = fr[ev->start - 2].stick_x;
    f->stick_x_hist[2] = ev->start >= 3 ? fr[ev->start - 3].stick_x : 0;

    float x0 = f->pos.x, y0 = f->pos.y, ry0 = s0->y;
    float top_eng = 0, top_real = 0;
    ev->max_err = 0;
    event_id++;
    for (int k = ev->start; k <= ev->end; k++) {
        Input in[2] = { map_input(&fr[k]), { 0 } };
        game_step(&g, in);
        float ex = f->pos.x - fr[k].x, ey = f->pos.y - fr[k].y;
        float err = sqrtf(ex * ex + ey * ey);
        if (err > ev->max_err) ev->max_err = err;
        ev->frames++;
        if (group_of(f->state) == real_group(&fr[k])) ev->state_match++;
        if (f->pos.y - y0 > top_eng) top_eng = f->pos.y - y0;
        if (fr[k].y - ry0 > top_real) top_real = fr[k].y - ry0;
        if (csv)
            fprintf(csv, "%d,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%d,%s\n", event_id, TECH_NAMES[ev->tech], ev->port + 1,
                    k + SLP_FIRST_FRAME, fr[k].x, fr[k].y, f->pos.x, f->pos.y, fr[k].action, state_name(f->state));
    }
    const SlpFrame *se = &fr[ev->end];
    ev->real_dx = se->x - s0->x;
    ev->eng_dx = f->pos.x - x0;
    ev->real_h = top_real;
    ev->eng_h = top_eng;
    float ex = f->pos.x - se->x, ey = f->pos.y - se->y;
    ev->end_err = sqrtf(ex * ex + ey * ey);
}

/* ---------- Techniken erkennen ---------- */

static bool started(const SlpFrame *fr, int k, uint16_t a) { return fr[k].action == a && fr[k - 1].action != a; }

/* Ende einer zusammenhängenden Phase mit Action a ab Frame k */
static int phase_end(const SlpFrame *fr, int n, int k, uint16_t a)
{
    while (k + 1 < n && fr[k + 1].action == a) k++;
    return k;
}

static void detect(const SlpGame *sg, int port, EventList *out)
{
    const SlpFrame *fr = sg->frames[port];
    int n = sg->nframes;
    float fh = 0, sh = 0;   /* Referenzhöhen aus der Engine, um SH/FH zu unterscheiden */
    {
        FighterParams p = fighter_params((CharId)map_char(sg->ext_char[port]));
        float g = p.gravity;
        fh = p.fullhop * p.fullhop / (2 * g) + p.fullhop / 2;
        sh = p.shorthop * p.shorthop / (2 * g) + p.shorthop / 2;
    }

    for (int k = 4; k < n - 1; k++) {
        if (!fr[k].has_post || !fr[k].has_pre) continue;

        /* Wavedash / Waveland: Beginn von LandingFallSpecial, davor Airdodge (oder Landung im selben
         * Frame wie der Airdodge). Kommt davor innerhalb weniger Frames ein Jumpsquat -> Wavedash. */
        if (started(fr, k, AS_LANDING_FALL_SPECIAL)) {
            int j = k - 1;
            while (j > 2 && k - j <= 12 && fr[j].action == AS_ESCAPE_AIR) j--;
            int dodge_start = j + 1;                       /* erster Airdodge-Frame (oder k) */
            int js = j;
            while (js > 2 && k - js <= 14 && (fr[js].action == AS_JUMP_F || fr[js].action == AS_JUMP_B)) js--;
            Event e = { 0 };
            e.port = port;
            e.end = phase_end(fr, n, k, AS_LANDING_FALL_SPECIAL);
            if (fr[js].action == AS_KNEE_BEND) {
                while (js > 2 && fr[js - 1].action == AS_KNEE_BEND) js--;
                e.tech = T_WAVEDASH;
                e.start = js;
            } else if (dodge_start < k || fr[k - 1].action != AS_ESCAPE_AIR) {
                if (dodge_start == k) continue;           /* LandingFallSpecial ohne Airdodge (z. B. Up-B) */
                e.tech = T_WAVELAND;
                e.start = dodge_start;
            } else continue;
            push(out, e);
            continue;
        }

        /* Dashdance: mindestens 3 Dashes mit wechselnder Richtung, je <= 16 Frames auseinander */
        if (started(fr, k, AS_DASH) && !(fr[k - 1].action == AS_TURN && started(fr, k - 1, AS_TURN) && 0)) {
            int dashes = 1, last = k;
            float face = fr[k].facing;
            /* menschliche Eingaben: kurze Walk/Wait/Turn/Brake-Frames zwischen den Dashes erlaubt */
            for (int j = k + 1; j < n && j <= last + 20; j++) {
                uint16_t a = fr[j].action;
                if (a != AS_DASH && a != AS_TURN && a != AS_WAIT && a != AS_RUN_BRAKE &&
                    a != AS_WALK_SLOW && a != AS_WALK_MID && a != AS_WALK_FAST) break;
                if (started(fr, j, AS_DASH) && fr[j].facing != face) {
                    dashes++;
                    last = j;
                    face = fr[j].facing;
                }
            }
            if (dashes >= 3) {
                Event e = { 0 };
                e.tech = T_DASHDANCE;
                e.port = port;
                e.start = k + 1;   /* Start im laufenden Dash: erster Dash-Auslöser hängt vom Vorzustand ab */
                e.end = phase_end(fr, n, last, AS_DASH);
                push(out, e);
                k = e.end;
            }
            continue;
        }

        /* Sprung ohne Aktion bis zum Scheitelpunkt */
        if (started(fr, k, AS_KNEE_BEND)) {
            int ks = k, ke = phase_end(fr, n, k, AS_KNEE_BEND);
            int t = ke + 1;
            if (t >= n || (fr[t].action != AS_JUMP_F && fr[t].action != AS_JUMP_B)) continue;
            int j = t;
            float y0 = fr[ks - 1].y, top = y0;
            bool clean = true;
            while (j + 1 < n && fr[j + 1].y >= fr[j].y) {
                j++;
                uint16_t a = fr[j].action;
                if (a != AS_JUMP_F && a != AS_JUMP_B) { clean = false; break; }
                top = fr[j].y;
            }
            if (!clean || j - t < 4) continue;
            float h = top - y0;
            Event e = { 0 };
            e.tech = fabsf(h - sh) < fabsf(h - fh) ? T_SHORTHOP : T_FULLHOP;
            e.port = port;
            e.start = ks;
            e.end = j;
            push(out, e);
            k = j;
        }
    }
}

/* ---------- Ausgabe ---------- */

static void fmt_time(int frame_idx, char *buf, size_t n)
{
    int f = frame_idx + SLP_FIRST_FRAME;
    if (f < 0) f = 0;
    snprintf(buf, n, "%d:%02d", f / 3600, (f / 60) % 60);
}

static int cmp_err(const void *a, const void *b)
{
    float x = ((const Event *)a)->max_err, y = ((const Event *)b)->max_err;
    return (x < y) - (x > y);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Aufruf: slp_analyze replay.slp [--csv frames.csv] [--top N]\n");
        return 2;
    }
    int top = 5;
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "--csv") == 0) csv = fopen(argv[i + 1], "w");
        if (strcmp(argv[i], "--top") == 0) top = atoi(argv[i + 1]);
    }
    SlpGame sg;
    char err[128];
    if (!slp_load(argv[1], &sg, err, sizeof(err))) { fprintf(stderr, "Fehler: %s\n", err); return 1; }

    printf("Replay: Slippi %d.%d.%d, %s, %d Frames (%.1f s)\n", sg.version[0], sg.version[1], sg.version[2],
           slp_stage_name(sg.stage), sg.nframes, sg.nframes / 60.0);
    if (map_stage(sg.stage) < 0) {
        printf("Stage wird nicht unterstützt (nur BF, FD, YS, DL, FoD, PS).\n");
        slp_free(&sg);
        return 1;
    }
    if (csv) fprintf(csv, "event,technik,port,frame,real_x,real_y,engine_x,engine_y,real_action,engine_state\n");

    for (int p = 0; p < SLP_PORTS; p++) {
        if (!sg.frames[p]) continue;
        int c = map_char(sg.ext_char[p]);
        printf("\n=== Port %d: %s", p + 1, slp_char_name(sg.ext_char[p]));
        if (c < 0) { printf(" (kein Gegenstück in der Engine, übersprungen)\n"); continue; }
        printf(" -> %s ===\n", fighter_params((CharId)c).name);

        EventList ev = { 0 };
        detect(&sg, p, &ev);
        for (int i = 0; i < ev.n; i++) simulate(&sg, &ev.v[i]);

        printf("%-10s %6s %10s %10s %10s %10s %10s\n", "Technik", "Anzahl", "Weg real", "Weg Engine", "Abw. Ende",
               "Abw. max", "Zustand=");
        for (int t = 0; t < T_COUNT; t++) {
            int cnt = 0;
            double rd = 0, ed = 0, ee = 0, me = 0, rh = 0, eh = 0;
            long fr_all = 0, fr_ok = 0;
            for (int i = 0; i < ev.n; i++) {
                const Event *e = &ev.v[i];
                if (e->tech != (Tech)t) continue;
                cnt++;
                rd += fabsf(e->real_dx); ed += fabsf(e->eng_dx);
                ee += e->end_err; me += e->max_err;
                rh += e->real_h; eh += e->eng_h;
                fr_all += e->frames; fr_ok += e->state_match;
            }
            if (!cnt) { printf("%-10s %6d\n", TECH_NAMES[t], 0); continue; }
            printf("%-10s %6d %10.2f %10.2f %10.2f %10.2f %9.0f%%", TECH_NAMES[t], cnt, rd / cnt, ed / cnt, ee / cnt,
                   me / cnt, fr_all ? 100.0 * (double)fr_ok / (double)fr_all : 0.0);
            if (t == T_SHORTHOP || t == T_FULLHOP) printf("   Höhe real %.2f / Engine %.2f", rh / cnt, eh / cnt);
            printf("\n");
        }

        qsort(ev.v, (size_t)ev.n, sizeof(Event), cmp_err);
        printf("Größte Abweichungen (zum Nachsehen in Slippi):\n");
        for (int i = 0; i < ev.n && i < top; i++) {
            const Event *e = &ev.v[i];
            char tm[16];
            fmt_time(e->start, tm, sizeof(tm));
            printf("  %-10s bei %s (Frame %d-%d): real %+.2f, Engine %+.2f, max. Abw. %.2f, Zustand gleich %d/%d\n",
                   TECH_NAMES[e->tech], tm, e->start + SLP_FIRST_FRAME, e->end + SLP_FIRST_FRAME, e->real_dx, e->eng_dx,
                   e->max_err, e->state_match, e->frames);
        }
        free(ev.v);
    }
    if (csv) fclose(csv);
    slp_free(&sg);
    return 0;
}
