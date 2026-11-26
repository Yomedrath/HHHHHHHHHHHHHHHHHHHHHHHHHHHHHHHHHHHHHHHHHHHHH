/* slp_techniques – findet Advanced Techniques in echten Slippi-Replays, schreibt jede Szene als
 * eigene .slp-Datei (echte Melee-Daten, z. B. in slippilab.com ansehbar) und rendert optional ein
 * Video mit Inputanzeige.
 *
 *   slp_techniques out_dir replay1.slp [replay2.slp ...] [--video] [--per 2] [--slow 2]
 *
 * Ergebnis in out_dir:  <technik>/<charakter>_<stage>_<untergrund>_<nr>.slp
 *                       techniques.md  (Übersicht + Abdeckung Technik × Charakter × Untergrund)
 *                       <technik>.mp4  (mit --video, braucht ffmpeg)                          */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slp.h"

#ifdef _WIN32
#include <direct.h>
#define popen _popen
#define pclose _pclose
#define PIPE_MODE "wb"
#else
#define PIPE_MODE "w"
#endif

/* ---------- Action States ---------- */

enum {
    A_WAIT = 14, A_WALK1 = 15, A_WALK3 = 17, A_TURN = 18, A_DASH = 20, A_RUN = 21, A_KNEE = 24,
    A_JUMPF = 25, A_JUMPB = 26, A_JUMPAF = 27, A_JUMPAB = 28, A_FALL = 29, A_FALLAB = 34,
    A_SQUAT = 39, A_SQUATWAIT = 40, A_SQUATRV = 41, A_LANDING = 42, A_LFS = 43,
    A_ATTACK11 = 44, A_ATTACKLW4 = 64, A_AIRN = 65, A_AIRLW = 69, A_LANDAIRN = 70, A_LANDAIRLW = 74,
    A_CATCH = 212, A_CATCHDASH = 214, A_ESCAPEAIR = 236, A_PASS = 244,
    A_CLIFFCATCH = 252, A_CLIFFWAIT = 253, A_SPECIAL = 341,
    A_SHINE_GROUND = 360, A_SHINE_GROUND_END = 363,       /* Fox/Falco Down-B am Boden */
    A_SHINE_AIR = 364, A_SHINE_AIR_END = 369,
};

static bool is_aerial(int a) { return a >= A_AIRN && a <= A_AIRLW; }
static bool is_aerial_landing(int a) { return a >= A_LANDAIRN && a <= A_LANDAIRLW; }
static bool is_ground_attack(int a) { return a >= A_ATTACK11 && a <= A_ATTACKLW4; }
static bool is_shine(int a) { return a >= A_SHINE_GROUND && a <= A_SHINE_AIR_END; }
static bool is_ground_shine(int a) { return a >= A_SHINE_GROUND && a <= A_SHINE_GROUND_END; }
static bool is_landing(int a) { return a == A_LANDING || a == A_LFS || is_aerial_landing(a); }
static bool is_walk(int a) { return a >= A_WALK1 && a <= A_WALK3; }

static const char *action_name(int a)
{
    static char buf[24];
    switch (a) {
    case 14: return "Wait"; case 15: case 16: case 17: return "Walk"; case 18: return "Turn";
    case 19: return "TurnRun"; case 20: return "Dash"; case 21: return "Run"; case 23: return "RunBrake";
    case 24: return "KneeBend"; case 25: return "JumpF"; case 26: return "JumpB";
    case 27: return "JumpAerialF"; case 28: return "JumpAerialB";
    case 29: case 30: case 31: return "Fall"; case 32: case 33: case 34: return "FallAerial";
    case 35: case 36: case 37: return "FallSpecial"; case 38: return "DamageFall";
    case 39: return "Squat"; case 40: return "SquatWait"; case 41: return "SquatRv";
    case 42: return "Landing"; case 43: return "LandingFallSpecial";
    case 44: case 45: case 46: return "Jab"; case 50: return "DashAttack";
    case 51: case 52: case 53: case 54: case 55: return "FTilt"; case 56: return "UTilt"; case 57: return "DTilt";
    case 58: case 59: case 60: case 61: case 62: return "FSmash"; case 63: return "USmash"; case 64: return "DSmash";
    case 65: return "Nair"; case 66: return "Fair"; case 67: return "Bair"; case 68: return "Uair"; case 69: return "Dair";
    case 70: return "LandingNair"; case 71: return "LandingFair"; case 72: return "LandingBair";
    case 73: return "LandingUair"; case 74: return "LandingDair";
    case 178: return "GuardOn"; case 179: return "Guard"; case 180: return "GuardOff";
    case 212: return "Grab"; case 213: return "GrabPull"; case 214: return "DashGrab";
    case 233: return "RollF"; case 234: return "RollB"; case 235: return "Spotdodge"; case 236: return "Airdodge";
    case 244: return "PlatformDrop"; case 252: return "CliffCatch"; case 253: return "CliffWait";
    default: break;
    }
    if (a >= 75 && a <= 91) return "Hitstun";
    if (a >= 183 && a <= 198) return "Down";
    if (a == 199) return "Tech"; if (a == 200) return "TechRollF"; if (a == 201) return "TechRollB";
    if (a >= 360 && a <= 363) return "Shine";
    if (a >= 364 && a <= 369) return "Shine (Luft)";
    if (a >= A_SPECIAL) { snprintf(buf, sizeof(buf), "Special %d", a); return buf; }
    snprintf(buf, sizeof(buf), "AS %d", a);
    return buf;
}

/* ---------- Techniken ---------- */

typedef enum {
    T_SHFFL, T_AUTOCANCEL, T_AERIAL_INTERRUPT, T_NIL, T_PCDROP, T_MULTISHINE, T_WAVESHINE,
    T_PIVOT, T_PIVOT_GRAB, T_EMPTY_PIVOT, T_PIVOT_ATTACK, T_EDGECANCEL, T_LEDGEDASH, T_LEDGESTALL,
    T_COUNT
} Tech;

static const char *TECH_ID[T_COUNT] = {
    "shffl", "autocancel", "aerial_interrupt", "no_impact_land", "pc_drop", "multishine", "waveshine",
    "pivot", "pivot_grab", "empty_pivot", "pivot_attack", "edgecancel_aerial", "ledgedash", "ledgestall",
};
static const char *TECH_NAME[T_COUNT] = {
    "SHFFL", "Autocanceled Aerial", "Aerial Interrupt", "No Impact Land", "PC Drop", "Multishine",
    "Waveshine", "Pivot (jede Art)", "Pivot Grab", "Empty Pivot", "Pivot in Angriff", "Edgecanceled Aerial",
    "Ledgedash", "Ledgestall",
};
static const char *TECH_DESC[T_COUNT] = {
    "Short Hop -> Aerial -> Fastfall -> L-Cancel bei der Landung (L-Cancel-Status 'erfolgreich').",
    "Aerial landet ausserhalb seines Landing-Lag-Fensters: Aerial geht direkt in normales Landing (4 Frames).",
    "Aerial wird in der Luft durch eine andere Aktion beendet (Doppelsprung, Airdodge, anderes Aerial, Special).",
    "Landung ohne Landing-Animation: Luftzustand geht direkt in Wait/Walk/Dash usw. ueber.",
    "PC Walk zur Kante: Walk -> 1 Frame Smash-Turn -> Schwung traegt ueber die Kante -> CliffCatch.",
    "Fox/Falco: Shine am Boden -> Jumpsquat -> Shine (mindestens zweimal hintereinander).",
    "Fox/Falco: Shine -> Jumpsquat -> Airdodge in den Boden (Wavedash aus dem Shine).",
    "Dash -> Smash-Turn, danach kein neuer Dash (Oberbegriff fuer alle Pivot-Varianten).",
    "Pivot -> Grab in die neue Richtung.",
    "Pivot ohne Folgeaktion (stehen bleiben, gehen, ducken).",
    "Pivot -> Bodenangriff (Tilt/Smash/Jab) in die neue Richtung.",
    "Landung eines Aerials an der Kante: der Landing-Lag wird durch Abrutschen von der Kante abgebrochen.",
    "Von der Kante loslassen -> Doppelsprung -> Airdodge/Waveland auf die Stage.",
    "Von der Kante weg und ohne Bodenkontakt wieder an die Kante (Unverwundbarkeit auffrischen). "
    "Charakterabhaengig; die Folge der Aktionen steht in der Liste.",
};

typedef struct {
    Tech tech;
    int port, start, end;           /* Kern der Technik (Frame-Indizes) */
    float surface_y;                /* Höhe des Untergrunds (Boden/Plattform) */
    bool surface_known;
    char note[96];
    char file[256];
    int replay;
} Event;

typedef struct { Event *v; int n, cap; } List;

static void add(List *l, Event e)
{
    if (l->n == l->cap) {
        int c = l->cap ? l->cap * 2 : 128;
        Event *nv = realloc(l->v, (size_t)c * sizeof(Event));
        if (!nv) return;
        l->v = nv;
        l->cap = c;
    }
    l->v[l->n++] = e;
}

static bool st(const SlpFrame *F, int k, int a) { return F[k].action == a && F[k - 1].action != a; }

static int end_of(const SlpFrame *F, int n, int k)
{
    int a = F[k].action;
    while (k + 1 < n && F[k + 1].action == a) k++;
    return k;
}

static Event mk(Tech t, int port, int s, int e)
{
    Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.tech = t;
    ev.port = port;
    ev.start = s;
    ev.end = e;
    return ev;
}

/* Untergrundhöhe: erster Bodenframe im Bereich */
static void set_surface(Event *e, const SlpFrame *F, int from, int to)
{
    for (int k = from; k <= to; k++)
        if (!F[k].airborne) { e->surface_y = F[k].y; e->surface_known = true; return; }
}

static void detect(const SlpGame *g, int port, List *out)
{
    const SlpFrame *F = g->frames[port];
    int n = g->nframes;
    bool spacie = g->ext_char[port] == 2 || g->ext_char[port] == 20;   /* Shine nur Fox/Falco */
    for (int k = 2; k < n - 2; k++) {
        int a = F[k].action, prev = F[k - 1].action;

        /* --- Landungen: SHFFL, Autocancel, Edgecancel, NIL --- */
        if (F[k - 1].airborne && !F[k].airborne) {
            if (is_aerial_landing(a) && F[k].lcancel == 1) {
                /* rückwärts: Aerial, davor Short Hop (Sprungtaste vor Jumpsquat-Ende losgelassen) */
                int j = k - 1;
                while (j > 2 && is_aerial(F[j].action)) j--;
                /* Fastfall an der Positionsänderung erkennen (ältere Replays haben keine Geschwindigkeiten):
                 * die Fallgeschwindigkeit springt in einem Frame um mehr als 0.5 nach unten */
                bool fastfall = false;
                for (int q = j + 2; q < k; q++) {
                    float v1 = F[q].y - F[q - 1].y, v0 = F[q - 1].y - F[q - 2].y;
                    if (v1 < v0 - 0.5f && v1 < 0) fastfall = true;
                }
                int js = j;
                while (js > 2 && (F[js].action == A_JUMPF || F[js].action == A_JUMPB)) js--;
                if (F[js].action == A_KNEE && fastfall && k - js < 45) {
                    const SlpFrame *last = &F[js];
                    bool sh = !(last->buttons & (SLP_BTN_X | SLP_BTN_Y)) && last->stick_y < 0.66f;
                    int ks = js;
                    while (ks > 2 && F[ks - 1].action == A_KNEE) ks--;
                    if (sh) {
                        Event e = mk(T_SHFFL, port, ks, end_of(F, n, k));
                        snprintf(e.note, sizeof(e.note), "%s, L-Cancel", action_name(F[k - 1].action));
                        set_surface(&e, F, k, k);
                        add(out, e);
                    }
                }
            }
            if (a == A_LANDING && is_aerial(prev)) {
                int j = k - 1;
                while (j > 2 && F[j - 1].action == prev) j--;
                Event e = mk(T_AUTOCANCEL, port, j, end_of(F, n, k));
                snprintf(e.note, sizeof(e.note), "%s", action_name(prev));
                set_surface(&e, F, k, k);
                add(out, e);
            }
            if (!is_landing(a) && !F[k].airborne && a != A_CLIFFCATCH && a < A_SPECIAL &&
                (prev >= A_JUMPF && prev <= A_FALLAB)) {
                Event e = mk(T_NIL, port, k - 6, k + 4);
                snprintf(e.note, sizeof(e.note), "%s -> %s", action_name(prev), action_name(a));
                set_surface(&e, F, k, k);
                add(out, e);
            }
        }
        /* Edgecancel: Aerial-Landung (oder Landing aus Aerial), dann wieder in der Luft */
        if ((is_aerial_landing(prev) || prev == A_LANDING) && F[k].airborne && !F[k - 1].airborne) {
            int j = k - 1;
            while (j > 2 && F[j - 1].action == prev) j--;
            if (is_aerial(F[j - 1].action) || is_aerial_landing(prev)) {
                int as = j - 1;
                while (as > 2 && is_aerial(F[as - 1].action)) as--;
                Event e = mk(T_EDGECANCEL, port, as, k + 8);
                snprintf(e.note, sizeof(e.note), "%s nach %d Frames abgebrochen", action_name(prev), k - j);
                set_surface(&e, F, j, k);
                add(out, e);
            }
        }

        /* --- Aerial Interrupt --- */
        if (is_aerial(prev) && F[k].airborne && a != prev && !is_aerial_landing(a) &&
            (a == A_JUMPAF || a == A_JUMPAB || a == A_ESCAPEAIR || is_aerial(a) || a >= A_SPECIAL)) {
            int j = k - 1;
            while (j > 2 && F[j - 1].action == prev) j--;
            Event e = mk(T_AERIAL_INTERRUPT, port, j, k + 10);
            snprintf(e.note, sizeof(e.note), "%s -> %s (Aerial-Frame %d)", action_name(prev), action_name(a), k - j);
            add(out, e);
        }

        /* --- Pivots --- */
        if (st(F, k, A_TURN) && prev == A_DASH) {
            int te = end_of(F, n, k);
            int nx = te + 1 < n ? F[te + 1].action : 0;
            if (nx != A_DASH && te + 1 < n) {
                int ds = k - 1;
                while (ds > 2 && F[ds - 1].action == A_DASH) ds--;
                Tech t = T_EMPTY_PIVOT;
                if (nx == A_CATCH || nx == A_CATCHDASH) t = T_PIVOT_GRAB;
                else if (is_ground_attack(nx)) t = T_PIVOT_ATTACK;
                else if (!(nx == A_WAIT || is_walk(nx) || nx == A_SQUAT || nx == A_SQUATWAIT)) t = T_PIVOT;
                Event e = mk(t, port, ds, end_of(F, n, te + 1));
                if (e.end > te + 40) e.end = te + 40;
                snprintf(e.note, sizeof(e.note), "Turn %d Frames -> %s", te - k + 1, action_name(nx));
                set_surface(&e, F, k, k);
                add(out, e);
                if (t != T_PIVOT) {
                    Event any = e;
                    any.tech = T_PIVOT;
                    add(out, any);
                }
            }
        }

        /* --- PC Drop: Walk -> Turn -> Kante greifen --- */
        if (st(F, k, A_CLIFFCATCH)) {
            int j = k - 1, turn = -1, walk = -1;
            for (; j > 2 && k - j < 30; j--) {
                if (!F[j].airborne && F[j].action == A_TURN && turn < 0) turn = j;
                if (turn >= 0 && is_walk(F[j].action)) { walk = j; break; }
                if (!F[j].airborne && turn < 0 && !(F[j].action == A_TURN)) break;
                if (F[j].action == A_KNEE || F[j].action == A_JUMPAF || F[j].action == A_JUMPAB) break;
            }
            if (walk >= 0) {
                Event e = mk(T_PCDROP, port, walk - 5, k + 10);
                snprintf(e.note, sizeof(e.note), "Walk -> Turn -> CliffCatch in %d Frames", k - walk);
                set_surface(&e, F, walk, walk);
                add(out, e);
            }
        }

        /* --- Shine-Techniken --- */
        /* Multishine: Shine -> Jumpsquat -> (1-2 Sprungframes) -> Shine (am Boden oder knapp darüber) ...
         * Der Jumpsquat lässt sich in Melee nicht mit Down-B unterbrechen, der Folge-Shine kommt im
         * ersten Luftframe. */
        if (spacie && st(F, k, A_SHINE_GROUND)) {
            int shines = 1, j = k, last = k, jumpframes = 0;
            while (j + 1 < n && j - last < 20) {
                j++;
                int aj = F[j].action;
                if (is_shine(aj) && !is_shine(F[j - 1].action)) {
                    if (F[j - 1].action == A_KNEE || F[j - 1].action == A_JUMPF || F[j - 1].action == A_JUMPB) {
                        shines++;
                        last = j;
                        jumpframes = 0;
                    } else break;
                } else if (aj == A_JUMPF || aj == A_JUMPB) {
                    if (++jumpframes > 2) break;
                } else if (!(is_shine(aj) || aj == A_KNEE)) break;
            }
            if (shines >= 2) {
                Event e = mk(T_MULTISHINE, port, k, end_of(F, n, last));
                snprintf(e.note, sizeof(e.note), "%d Shines", shines);
                set_surface(&e, F, k, k);
                add(out, e);
                k = last;
            }
        }
        if (st(F, k, A_LFS)) {
            int j = k - 1;
            while (j > 2 && k - j < 16 && (F[j].action == A_ESCAPEAIR || F[j].action == A_JUMPF ||
                                            F[j].action == A_JUMPB || F[j].action == A_KNEE)) j--;
            if (spacie && is_shine(F[j].action) && k - j > 1) {
                int s = j;
                while (s > 2 && is_shine(F[s - 1].action)) s--;
                Event e = mk(T_WAVESHINE, port, s, end_of(F, n, k));
                snprintf(e.note, sizeof(e.note), "Shine -> Wavedash, %d Frames", k - j);
                set_surface(&e, F, k, k);
                add(out, e);
            }
            /* Ledgedash: kurz vorher an der Kante */
            int c = k - 1;
            while (c > 2 && k - c < 40 && F[c].action != A_CLIFFWAIT && F[c].action != A_CLIFFCATCH &&
                   F[c].airborne) c--;
            if ((F[c].action == A_CLIFFWAIT || F[c].action == A_CLIFFCATCH) && k - c < 40) {
                bool jumped = false;
                for (int q = c; q < k; q++) if (F[q].action == A_JUMPAF || F[q].action == A_JUMPAB) jumped = true;
                if (jumped) {
                    Event e = mk(T_LEDGEDASH, port, c - 5, end_of(F, n, k));
                    snprintf(e.note, sizeof(e.note), "Kante -> Waveland in %d Frames", k - c);
                    set_surface(&e, F, k, k);
                    add(out, e);
                }
            }
        }

        /* --- Ledgestall: Kante verlassen und ohne Bodenkontakt wieder greifen --- */
        if (st(F, k, A_CLIFFCATCH)) {
            int j = k - 1;
            /* Stall = schnelles Loslassen und Wiedergreifen (<= 35 Frames), nicht eine normale Recovery */
            while (j > 2 && k - j < 40 && F[j].airborne && F[j].action != A_CLIFFWAIT && F[j].action != A_CLIFFCATCH) j--;
            if ((F[j].action == A_CLIFFWAIT || F[j].action == A_CLIFFCATCH) && k - j >= 2 && k - j <= 35) {
                Event e = mk(T_LEDGESTALL, port, j - 10, k + 10);
                char seq[96] = "";
                int lasta = -1;
                for (int q = j + 1; q < k && strlen(seq) < 70; q++) {
                    if (F[q].action == lasta) continue;
                    lasta = F[q].action;
                    if (seq[0]) strcat(seq, " > ");
                    strncat(seq, action_name(lasta), sizeof(seq) - strlen(seq) - 1);
                }
                snprintf(e.note, sizeof(e.note), "%s", seq);
                add(out, e);
            }
        }
        (void)a;
    }
}

/* ---------- Ausgabehilfen ---------- */

static void mkdir_p(const char *path)
{
    SDL_CreateDirectory(path);
}

static const char *stage_short(int s)
{
    switch (s) {
    case 2: return "FoD"; case 3: return "PS"; case 8: return "YS"; case 28: return "DL";
    case 31: return "BF"; case 32: return "FD"; default: return "Stage";
    }
}

static int g_stage_for_label = 0;   /* Stage des gerade beschrifteten Events */

/* Untergrund einordnen: Hauptplattform (inkl. Schrägen von YS, FoD bei 0.6) oder bekannte Plattform;
 * bewegliche FoD-Seitenplattformen werden in 4er-Höhenstufen gruppiert. */
static void surface_label(const Event *e, char *buf, size_t n)
{
    static const struct { int stage; float y[2]; } P[] = {
        { 31, { 27.2f, 54.4f } }, { 8, { 23.45f, 42.0f } }, { 28, { 30.13f, 51.42f } },
        { 2, { 42.75f, 0 } }, { 3, { 25.0f, 0 } },
    };
    if (!e->surface_known) { snprintf(buf, n, "Luft"); return; }
    float y = e->surface_y;
    if (y < 5.0f) { snprintf(buf, n, "Boden"); return; }
    for (size_t i = 0; i < sizeof(P) / sizeof(P[0]); i++) {
        if (P[i].stage != g_stage_for_label) continue;
        for (int k = 0; k < 2; k++)
            if (P[i].y[k] > 0 && fabsf(y - P[i].y[k]) < 1.5f) { snprintf(buf, n, "Plattform %.1f", P[i].y[k]); return; }
    }
    if (g_stage_for_label == 2) {
        int lo = (int)(y / 4.0f) * 4;
        snprintf(buf, n, "Seitenplattform %d-%d", lo, lo + 4);
        return;
    }
    snprintf(buf, n, "Plattform %.0f", y);
}

static void file_char(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 1 < n; s++) {
        char c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out[o++] = c;
        else if (c == ' ' || c == '.') out[o++] = '_';
    }
    out[o] = 0;
}

/* ---------- Video ---------- */

#define VW 1280
#define VH 720

typedef struct { float cx, cy, scale; } Cam;

static SDL_FPoint w2s(Cam c, float x, float y)
{
    SDL_FPoint p = { (x - c.cx) * c.scale + VW / 2.0f, VH / 2.0f - (y - c.cy) * c.scale };
    return p;
}

static void col(SDL_Renderer *r, uint32_t rgb) { SDL_SetRenderDrawColor(r, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255); }

static void txt(SDL_Renderer *r, float x, float y, float s, const char *t)
{
    SDL_SetRenderScale(r, s, s);
    SDL_RenderDebugText(r, x / s, y / s, t);
    SDL_SetRenderScale(r, 1, 1);
}

static void wrect(SDL_Renderer *r, Cam c, float x0, float y0, float x1, float y1, bool fill)
{
    SDL_FPoint a = w2s(c, x0, y1), b = w2s(c, x1, y0);
    SDL_FRect q = { a.x, a.y, b.x - a.x, b.y - a.y };
    if (fill) SDL_RenderFillRect(r, &q); else SDL_RenderRect(r, &q);
}

/* Stage-Geometrie (nur zur Orientierung, gleiche Maße wie in der Engine) */
typedef struct { float x0, x1, y; bool solid; } Surf;
static int stage_surfs(int s, Surf *o)
{
    switch (s) {
    case 31: o[0] = (Surf){ -68.4f, 68.4f, 0, true }; o[1] = (Surf){ -57.6f, -20, 27.2f, false };
             o[2] = (Surf){ 20, 57.6f, 27.2f, false }; o[3] = (Surf){ -18.8f, 18.8f, 54.4f, false }; return 4;
    case 32: o[0] = (Surf){ -85.56f, 85.56f, 0, true }; return 1;
    case 8:  o[0] = (Surf){ -56, 56, 0, true }; o[1] = (Surf){ -59.5f, -28, 23.45f, false };
             o[2] = (Surf){ 28, 59.5f, 23.45f, false }; o[3] = (Surf){ -15.75f, 15.75f, 42, false }; return 4;
    case 28: o[0] = (Surf){ -77.27f, 77.27f, 0, true }; o[1] = (Surf){ -61.39f, -30.02f, 30.13f, false };
             o[2] = (Surf){ 30.02f, 61.39f, 30.13f, false }; o[3] = (Surf){ -19.02f, 19.02f, 51.42f, false }; return 4;
    case 2:  o[0] = (Surf){ -63.35f, 63.35f, 0, true }; o[1] = (Surf){ -49.5f, -21, 27.12f, false };
             o[2] = (Surf){ 21, 49.5f, 27.12f, false }; o[3] = (Surf){ -14.25f, 14.25f, 43.83f, false }; return 4;
    case 3:  o[0] = (Surf){ -87.75f, 87.75f, 0, true }; o[1] = (Surf){ -55, -25, 25, false };
             o[2] = (Surf){ 25, 55, 25, false }; return 3;
    default: return 0;
    }
}

static const uint32_t PORT_COL[4] = { 0xE04040, 0x4070E0, 0xE0C030, 0x40B060 };

static void draw_inputs(SDL_Renderer *r, const SlpFrame *f, float x, float y)
{
    /* Stick */
    col(r, 0x303540);
    SDL_FRect box = { x, y, 110, 110 };
    SDL_RenderFillRect(r, &box);
    col(r, 0x808890);
    SDL_RenderRect(r, &box);
    SDL_RenderLine(r, x + 55, y + 5, x + 55, y + 105);
    SDL_RenderLine(r, x + 5, y + 55, x + 105, y + 55);
    col(r, 0xFFFFFF);
    SDL_FRect dot = { x + 55 + f->stick_x * 50 - 5, y + 55 - f->stick_y * 50 - 5, 10, 10 };
    SDL_RenderFillRect(r, &dot);
    col(r, 0xFFD040);
    SDL_FRect cdot = { x + 55 + f->cstick_x * 50 - 3, y + 55 - f->cstick_y * 50 - 3, 6, 6 };
    SDL_RenderFillRect(r, &cdot);
    char s[48];
    snprintf(s, sizeof(s), "%+.2f %+.2f", f->stick_x, f->stick_y);
    col(r, 0xC0C0C0);
    txt(r, x, y + 118, 1.25f, s);
    /* Tasten */
    static const struct { const char *n; uint32_t m; } B[] = {
        { "A", SLP_BTN_A }, { "B", SLP_BTN_B }, { "X", SLP_BTN_X }, { "Y", SLP_BTN_Y },
        { "L", SLP_BTN_L }, { "R", SLP_BTN_R }, { "Z", SLP_BTN_Z },
    };
    for (int i = 0; i < 7; i++) {
        bool on = (f->buttons & B[i].m) != 0;
        if (i == 4 && f->phys_l > 0.3f) on = true;
        if (i == 5 && f->phys_r > 0.3f) on = true;
        SDL_FRect b = { x + 125 + (float)(i % 4) * 44, y + (float)(i / 4) * 44 + 10, 38, 38 };
        col(r, on ? 0x40D070 : 0x303540);
        SDL_RenderFillRect(r, &b);
        col(r, 0x808890);
        SDL_RenderRect(r, &b);
        col(r, 0xFFFFFF);
        txt(r, b.x + 11, b.y + 11, 2, B[i].n);
    }
    snprintf(s, sizeof(s), "Trigger %.2f", f->trigger);
    col(r, 0xC0C0C0);
    txt(r, x + 125, y + 104, 1.25f, s);
}

static void render_event(SDL_Renderer *r, SDL_Surface *surf, FILE *pipe, const SlpGame *g, const Event *e,
                         int idx, int total, int slow)
{
    int pad0 = 20, pad1 = 25;
    int s = e->start - pad0 < 1 ? 1 : e->start - pad0;
    int t = e->end + pad1 >= g->nframes ? g->nframes - 1 : e->end + pad1;
    const SlpFrame *F = g->frames[e->port];
    Surf su[8];
    int ns = stage_surfs(g->stage, su);

    /* Kamera: Bereich der Technik */
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int k = s; k <= t; k++) {
        x0 = fminf(x0, F[k].x); x1 = fmaxf(x1, F[k].x);
        y0 = fminf(y0, F[k].y); y1 = fmaxf(y1, F[k].y);
    }
    Cam c;
    c.cx = (x0 + x1) * 0.5f;
    c.cy = (y0 + y1) * 0.5f + 8;
    float wspan = fmaxf(x1 - x0 + 50, 90), hspan = fmaxf(y1 - y0 + 45, 60);
    c.scale = fminf(VW * 0.8f / wspan, VH * 0.6f / hspan);

    char title[160], sub[200], surfl[32];
    g_stage_for_label = g->stage;
    surface_label(e, surfl, sizeof(surfl));
    snprintf(title, sizeof(title), "%s  (%d/%d)", TECH_NAME[e->tech], idx, total);
    snprintf(sub, sizeof(sub), "%s, Port %d, %s, %s | %s", slp_char_name(g->ext_char[e->port]), e->port + 1,
             slp_stage_name(g->stage), surfl, e->note);

    for (int k = s; k <= t; k++) {
        for (int rep = 0; rep < slow; rep++) {
            col(r, 0x14161C);
            SDL_RenderClear(r);
            for (int i = 0; i < ns; i++) {
                col(r, su[i].solid ? 0x4A5060 : 0x8890A0);
                if (su[i].solid) wrect(r, c, su[i].x0, -30, su[i].x1, su[i].y, true);
                else wrect(r, c, su[i].x0, su[i].y - 1.0f, su[i].x1, su[i].y, true);
            }
            for (int p = 0; p < SLP_PORTS; p++) {
                if (!g->frames[p]) continue;
                const SlpFrame *f = &g->frames[p][k];
                bool me = p == e->port;
                float h = 14, w = 6;
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                uint32_t cc = PORT_COL[p];
                SDL_SetRenderDrawColor(r, (cc >> 16) & 255, (cc >> 8) & 255, cc & 255, me ? 255 : 90);
                wrect(r, c, f->x - w / 2, f->y, f->x + w / 2, f->y + h, true);
                SDL_SetRenderDrawColor(r, 255, 255, 255, me ? 255 : 90);
                float nx = f->x + (f->facing < 0 ? -w / 2 - 2.5f : w / 2);
                wrect(r, c, nx, f->y + h * 0.7f, nx + 2.5f, f->y + h * 0.7f + 2.5f, true);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                if (me) {
                    SDL_FPoint hp = w2s(c, f->x, f->y + h + 3);
                    char an[64];
                    snprintf(an, sizeof(an), "%s %.0f", action_name(f->action), f->action_frame);
                    col(r, 0xFFFFFF);
                    txt(r, hp.x - (float)strlen(an) * 8, hp.y - 22, 2, an);
                    if (f->lcancel == 1 && is_aerial_landing(f->action)) { col(r, 0x40D070); txt(r, hp.x - 64, hp.y - 44, 2, "L-CANCEL"); }
                }
            }
            /* Kopfzeile, Frame-Zähler, Inputs */
            bool core = k >= e->start && k <= e->end;
            col(r, 0x0C0D11);
            SDL_FRect band = { 0, 0, VW, 100 };
            SDL_RenderFillRect(r, &band);
            SDL_FRect band2 = { 0, VH - 170, 330, 170 };
            SDL_RenderFillRect(r, &band2);
            col(r, 0xFFD040);
            txt(r, 20, 16, 3, title);
            col(r, 0xC8C8C8);
            txt(r, 20, 50, 1.5f, sub);
            char fl[96];
            snprintf(fl, sizeof(fl), "Frame %+d   (Replay %d)%s", k - e->start, k + SLP_FIRST_FRAME,
                     slow > 1 ? "   Zeitlupe" : "");
            col(r, core ? 0x40D070 : 0x808080);
            txt(r, 20, 74, 2, fl);
            draw_inputs(r, &F[k], 20, VH - 160);
            SDL_RenderPresent(r);
            for (int y = 0; y < VH; y++)
                fwrite((const uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch, 4, VW, pipe);
        }
    }
}

/* ---------- Hauptprogramm ---------- */

typedef struct { SlpGame g; char name[256]; } Loaded;

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Aufruf: slp_techniques out_dir replay.slp [...] [--video] [--per N] [--slow N]\n");
        return 2;
    }
    const char *outdir = argv[1];
    bool video = false;
    int per = 2, slow = 2;
    Loaded *L = calloc((size_t)argc, sizeof(Loaded));
    int nl = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--video") == 0) { video = true; continue; }
        if (strcmp(argv[i], "--per") == 0 && i + 1 < argc) { per = atoi(argv[++i]); continue; }
        if (strcmp(argv[i], "--slow") == 0 && i + 1 < argc) { slow = atoi(argv[++i]); continue; }
        char err[128];
        if (!slp_load(argv[i], &L[nl].g, err, sizeof(err))) { fprintf(stderr, "%s: %s\n", argv[i], err); continue; }
        snprintf(L[nl].name, sizeof(L[nl].name), "%s", argv[i]);
        nl++;
    }
    if (!nl) return 1;
    mkdir_p(outdir);

    List ev[64] = { 0 };   /* pro Replay */
    int count[T_COUNT] = { 0 };
    for (int r = 0; r < nl; r++) {
        for (int p = 0; p < SLP_PORTS; p++)
            if (L[r].g.frames[p]) detect(&L[r].g, p, &ev[r]);
    }

    /* Clips schreiben */
    char path[512];
    for (int t = 0; t < T_COUNT; t++) {
        snprintf(path, sizeof(path), "%s/%s", outdir, TECH_ID[t]);
        mkdir_p(path);
    }
    for (int r = 0; r < nl; r++) {
        for (int i = 0; i < ev[r].n; i++) {
            Event *e = &ev[r].v[i];
            const SlpGame *g = &L[r].g;
            char ch[32], sl[32], sl2[32];
            file_char(slp_char_name(g->ext_char[e->port]), ch, sizeof(ch));
            g_stage_for_label = L[r].g.stage;
                    g_stage_for_label = g->stage;
            surface_label(e, sl, sizeof(sl));
            file_char(sl, sl2, sizeof(sl2));
            int nr = ++count[e->tech];
            snprintf(e->file, sizeof(e->file), "%s/%s_%s_%s_%03d.slp", TECH_ID[e->tech], ch, stage_short(g->stage), sl2, nr);
            snprintf(path, sizeof(path), "%s/%s", outdir, e->file);
            int a = e->start - 30 < 0 ? 0 : e->start - 30;
            int b = e->end + 30 >= g->nframes ? g->nframes - 1 : e->end + 30;
            if (!slp_write_clip(g, a, b, path)) fprintf(stderr, "Clip fehlgeschlagen: %s\n", path);
        }
    }

    /* maschinenlesbarer Index */
    snprintf(path, sizeof(path), "%s/index.csv", outdir);
    FILE *ix = fopen(path, "w");
    if (ix) {
        fprintf(ix, "datei,technik,charakter,stage,untergrund,untergrund_y,replay,frame,notiz\n");
        for (int r = 0; r < nl; r++)
            for (int i = 0; i < ev[r].n; i++) {
                Event *e = &ev[r].v[i];
                char sl[32];
                g_stage_for_label = L[r].g.stage;
                    surface_label(e, sl, sizeof(sl));
                const char *base = strrchr(L[r].name, '/');
                fprintf(ix, "%s,%s,%s,%s,%s,%.2f,%s,%d,\"%s\"\n", e->file, TECH_ID[e->tech],
                        slp_char_name(L[r].g.ext_char[e->port]), stage_short(L[r].g.stage), sl,
                        e->surface_known ? e->surface_y : 0.0f, base ? base + 1 : L[r].name,
                        e->start + SLP_FIRST_FRAME, e->note);
            }
        fclose(ix);
    }

    /* Übersicht */
    snprintf(path, sizeof(path), "%s/techniques.md", outdir);
    FILE *md = fopen(path, "w");
    if (md) {
        fprintf(md, "# Advanced Techniques aus %d Replay(s)\n\n", nl);
        fprintf(md, "Jede Szene liegt als eigene `.slp` im Unterordner der Technik (30 Frames Vorlauf/Nachlauf).\n");
        fprintf(md, "Ansehen z. B. im Browser auf slippilab.com oder im Slippi Launcher.\n\n");
        fprintf(md, "## Abdeckung (Technik x Charakter x Untergrund)\n\n| Technik | Charakter | Stage | Untergrund | Anzahl |\n|---|---|---|---|---|\n");
        for (int t = 0; t < T_COUNT; t++) {
            bool any = false;
            /* einfache Gruppierung über alle Replays */
            for (int r = 0; r < nl; r++)
                for (int i = 0; i < ev[r].n; i++) {
                    Event *e = &ev[r].v[i];
                    if (e->tech != (Tech)t) continue;
                    char sl[32];
                    g_stage_for_label = L[r].g.stage;
                    surface_label(e, sl, sizeof(sl));
                    bool seen = false;
                    for (int r2 = 0; r2 <= r && !seen; r2++)
                        for (int j = 0; j < (r2 == r ? i : ev[r2].n); j++) {
                            Event *o = &ev[r2].v[j];
                            char sl3[32];
                            g_stage_for_label = L[r2].g.stage;
                            surface_label(o, sl3, sizeof(sl3));
                            if (o->tech == e->tech && L[r2].g.ext_char[o->port] == L[r].g.ext_char[e->port] &&
                                L[r2].g.stage == L[r].g.stage && strcmp(sl, sl3) == 0) { seen = true; break; }
                        }
                    if (seen) continue;
                    int cnt = 0;
                    for (int r2 = 0; r2 < nl; r2++)
                        for (int j = 0; j < ev[r2].n; j++) {
                            Event *o = &ev[r2].v[j];
                            char sl3[32];
                            g_stage_for_label = L[r2].g.stage;
                            surface_label(o, sl3, sizeof(sl3));
                            if (o->tech == e->tech && L[r2].g.ext_char[o->port] == L[r].g.ext_char[e->port] &&
                                L[r2].g.stage == L[r].g.stage && strcmp(sl, sl3) == 0) cnt++;
                        }
                    fprintf(md, "| %s | %s | %s | %s | %d |\n", TECH_NAME[t], slp_char_name(L[r].g.ext_char[e->port]),
                            stage_short(L[r].g.stage), sl, cnt);
                    any = true;
                }
            if (!any) fprintf(md, "| %s | – | – | – | **0 (fehlt)** |\n", TECH_NAME[t]);
        }
        fprintf(md, "\n## Techniken\n");
        for (int t = 0; t < T_COUNT; t++) {
            fprintf(md, "\n### %s (%d)\n\n%s\n\n", TECH_NAME[t], count[t], TECH_DESC[t]);
            int shown = 0;
            for (int r = 0; r < nl; r++)
                for (int i = 0; i < ev[r].n && shown < 40; i++) {
                    Event *e = &ev[r].v[i];
                    if (e->tech != (Tech)t) continue;
                    int f = e->start + SLP_FIRST_FRAME;
                    if (f < 0) f = 0;
                    fprintf(md, "- `%s` – %d:%02d, %s\n", e->file, f / 3600, (f / 60) % 60, e->note);
                    shown++;
                }
        }
        fclose(md);
    }

    printf("%-22s %s\n", "Technik", "Anzahl");
    for (int t = 0; t < T_COUNT; t++) printf("%-22s %d\n", TECH_NAME[t], count[t]);

    /* Video: pro Technik bis zu `per` Beispiele, in Zeitlupe */
    if (video) {
        SDL_Init(0);
        SDL_Surface *surf = SDL_CreateSurface(VW, VH, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer *rr = SDL_CreateSoftwareRenderer(surf);
        snprintf(path, sizeof(path), "%s/alle_techniken.mp4", outdir);
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -loglevel error -y -f rawvideo -pixel_format bgra -video_size %dx%d -framerate 60 -i - "
                 "-c:v libx264 -preset veryfast -crf 22 -pix_fmt yuv420p \"%s\"", VW, VH, path);
        FILE *pipe = popen(cmd, PIPE_MODE);
        if (!pipe) { fprintf(stderr, "ffmpeg nicht gefunden\n"); return 1; }
        int total = 0;
        for (int t = 0; t < T_COUNT; t++) total += count[t] < per ? count[t] : per;
        int idx = 0;
        for (int t = 0; t < T_COUNT; t++) {
            /* Beispiele möglichst aus unterschiedlichen Situationen: gleichmäßig verteilt */
            int shown = 0;
            for (int pick = 0; pick < per && pick < count[t]; pick++) {
                int want = (int)((float)(pick + 0.5f) * (float)count[t] / (float)(per < count[t] ? per : count[t]));
                int seen = 0;
                for (int r = 0; r < nl; r++)
                    for (int i = 0; i < ev[r].n; i++) {
                        Event *e = &ev[r].v[i];
                        if (e->tech != (Tech)t) continue;
                        if (seen++ == want) {
                            render_event(rr, surf, pipe, &L[r].g, e, ++idx, total, slow);
                            shown++;
                        }
                    }
            }
            (void)shown;
        }
        int rc = pclose(pipe);
        printf("%s: %s\n", rc == 0 ? "Video" : "ffmpeg-Fehler", path);
        SDL_DestroyRenderer(rr);
        SDL_DestroySurface(surf);
        SDL_Quit();
    }
    for (int r = 0; r < nl; r++) { free(ev[r].v); slp_free(&L[r].g); }
    free(L);
    return 0;
}
