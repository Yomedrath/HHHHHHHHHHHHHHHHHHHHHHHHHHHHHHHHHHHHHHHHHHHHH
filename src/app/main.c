/* proto1v1 – SDL3-Frontend: Menü, Eingabe (Tastatur / Gamepad / GC-Adapter), Platzhalter-Grafik.
 *
 * Aufruf:  proto1v1                              normales Spiel (Replays landen in replays/)
 *          proto1v1 datei.p1v1r                  Replay im Fenster abspielen (auch per Drag & Drop)
 *          proto1v1 --video out.mp4 [--replay datei.p1v1r]
 *                                                Video rendern (braucht ffmpeg im PATH);
 *                                                ohne --replay: CPU gegen CPU
 *          proto1v1 --screenshot out.bmp         Einzelbild
 *          Optionen ohne Replay: --stage 1..6 --p1 0..3 --p2 0..3 --frames N
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#define PIPE_MODE "wb"
#else
#define PIPE_MODE "w"
#endif

#define WIN_W 1280
#define WIN_H 720
#define MAX_PADS 2

/* ---------------- Eingabe ---------------- */

typedef struct {
    SDL_Gamepad *pads[MAX_PADS];
} Devices;

static float axis(SDL_Gamepad *g, SDL_GamepadAxis a)
{
    return (float)SDL_GetGamepadAxis(g, a) / 32767.0f;
}

/* GC-Sticks erreichen in SDL meist nur ~0.8 → hochskalieren, kleine Totzone */
static float stick_norm(float v)
{
    if (fabsf(v) < 0.12f) return 0.0f;
    v /= 0.8f;
    return v > 1 ? 1 : (v < -1 ? -1 : v);
}

static void read_pad(SDL_Gamepad *g, Input *in)
{
    in->stick_x = stick_norm(axis(g, SDL_GAMEPAD_AXIS_LEFTX));
    in->stick_y = -stick_norm(axis(g, SDL_GAMEPAD_AXIS_LEFTY));
    in->cstick_x = stick_norm(axis(g, SDL_GAMEPAD_AXIS_RIGHTX));
    in->cstick_y = -stick_norm(axis(g, SDL_GAMEPAD_AXIS_RIGHTY));

    static const SDL_GamepadButton face[4] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH
    };
    for (int i = 0; i < 4; i++) {
        if (!SDL_GetGamepadButton(g, face[i])) continue;
        /* Beschriftung statt Position benutzen → GC-Controller: A = Angriff, X/Y = Sprung */
        switch (SDL_GetGamepadButtonLabel(g, face[i])) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: in->a = true; break;
        case SDL_GAMEPAD_BUTTON_LABEL_B: in->b = true; break;
        case SDL_GAMEPAD_BUTTON_LABEL_X:
        case SDL_GAMEPAD_BUTTON_LABEL_Y: in->jump = true; break;
        default:
            if (i == 0) in->a = true;
            else if (i == 1) in->b = true;
            else in->jump = true;
            break;
        }
    }
    if (axis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.3f || axis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f ||
        SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
        in->shield = true;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) in->z = true;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)) in->start = true;
}

typedef struct {
    SDL_Scancode left, right, up, down, tilt, a, b, jump, jump2, shield, start;
} KeyLayout;

static const KeyLayout KEYS[2] = {
    { SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_LSHIFT,
      SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_SPACE, SDL_SCANCODE_I, SDL_SCANCODE_L, SDL_SCANCODE_RETURN },
    { SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_RSHIFT,
      SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_2,
      SDL_SCANCODE_KP_ENTER },
};

static void read_keys(const bool *ks, const KeyLayout *k, Input *in)
{
    float m = ks[k->tilt] ? 0.5f : 1.0f;           /* Shift = halbe Auslenkung (Gehen) */
    float x = (ks[k->right] ? 1.0f : 0.0f) - (ks[k->left] ? 1.0f : 0.0f);
    float y = (ks[k->up] ? 1.0f : 0.0f) - (ks[k->down] ? 1.0f : 0.0f);
    if (fabsf(x) > fabsf(in->stick_x)) in->stick_x = x * m;
    if (fabsf(y) > fabsf(in->stick_y)) in->stick_y = y * m;
    in->a |= ks[k->a];
    in->b |= ks[k->b];
    in->jump |= ks[k->jump] || ks[k->jump2];
    in->shield |= ks[k->shield];
    in->start |= ks[k->start];
}

static Input read_player(const Devices *d, int player)
{
    Input in;
    memset(&in, 0, sizeof(in));
    if (d->pads[player]) read_pad(d->pads[player], &in);
    read_keys(SDL_GetKeyboardState(NULL), &KEYS[player], &in);
    return in;
}

/* ---------------- Zeichnen ---------------- */

typedef struct { float cx, cy, scale; bool init; } Camera;

static void set_color(SDL_Renderer *r, uint32_t rgb, uint8_t a)
{
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}

static void text(SDL_Renderer *r, float x, float y, float scale, const char *s)
{
    SDL_SetRenderScale(r, scale, scale);
    SDL_RenderDebugText(r, x / scale, y / scale, s);
    SDL_SetRenderScale(r, 1, 1);
}

static float text_w(const char *s, float scale) { return (float)strlen(s) * 8.0f * scale; }

static void text_center(SDL_Renderer *r, float cx, float y, float scale, const char *s)
{
    text(r, cx - text_w(s, scale) * 0.5f, y, scale, s);
}

static SDL_FPoint w2s(const Camera *c, int w, int h, float x, float y)
{
    SDL_FPoint p = { (x - c->cx) * c->scale + (float)w * 0.5f, (float)h * 0.5f - (y - c->cy) * c->scale };
    return p;
}

static void world_rect(SDL_Renderer *r, const Camera *c, int w, int h, float x0, float y0, float x1, float y1, bool fill)
{
    SDL_FPoint a = w2s(c, w, h, x0, y1), b = w2s(c, w, h, x1, y0);
    SDL_FRect rect = { a.x, a.y, b.x - a.x, b.y - a.y };
    if (fill) SDL_RenderFillRect(r, &rect); else SDL_RenderRect(r, &rect);
}

static void world_circle(SDL_Renderer *r, const Camera *c, int w, int h, Vec2 ctr, float rad)
{
    SDL_FPoint pts[33];
    for (int i = 0; i <= 32; i++) {
        float t = (float)i / 32.0f * 6.2831853f;
        pts[i] = w2s(c, w, h, ctr.x + cosf(t) * rad, ctr.y + sinf(t) * rad);
    }
    SDL_RenderLines(r, pts, 33);
}

static void update_camera(Camera *c, const Game *g, int w, int h, bool snap)
{
    const Stage *st = g->stage;
    /* ganze Stage (inkl. Plattformen) bleibt immer im Bild */
    float x0 = 0, x1 = 0, y0 = -45, y1 = 75;
    for (int i = 0; i < st->nsurf; i++) {
        x0 = fminf(x0, st->surf[i].x0 - 25);
        x1 = fmaxf(x1, st->surf[i].x1 + 25);
    }
    for (int i = 0; i < MAX_FIGHTERS; i++) {
        const Fighter *f = &g->f[i];
        if (f->state == ST_DEAD) continue;
        x0 = fminf(x0, f->pos.x - 25); x1 = fmaxf(x1, f->pos.x + 25);
        y0 = fminf(y0, f->pos.y - 20); y1 = fmaxf(y1, f->pos.y + f->p.height + 25);
    }
    x0 = fmaxf(x0, st->blast_left);  x1 = fminf(x1, st->blast_right);
    y0 = fmaxf(y0, st->blast_bottom); y1 = fminf(y1, st->blast_top);
    float tcx = (x0 + x1) * 0.5f, tcy = (y0 + y1) * 0.5f;
    float ts = fminf((float)w / (x1 - x0), (float)h / (y1 - y0));
    if (snap || !c->init) { c->cx = tcx; c->cy = tcy; c->scale = ts; c->init = true; return; }
    c->cx += (tcx - c->cx) * 0.12f;
    c->cy += (tcy - c->cy) * 0.12f;
    c->scale += (ts - c->scale) * 0.08f;
}

static void draw_match(SDL_Renderer *r, int w, int h, const Game *g, const Camera *c, bool debug)
{
    const Stage *st = g->stage;
    set_color(r, 0x14161C, 255);
    SDL_RenderClear(r);

    /* Blastzones */
    set_color(r, 0x5A2A2A, 255);
    world_rect(r, c, w, h, st->blast_left, st->blast_bottom, st->blast_right, st->blast_top, false);

    /* Stage */
    for (int i = 0; i < st->nsurf; i++) {
        const Surface *s = &st->surf[i];
        if (s->kind == SURF_SOLID) {
            set_color(r, 0x4A5060, 255);
            world_rect(r, c, w, h, s->x0, s->bottom, s->x1, s->y, true);
            set_color(r, 0xA0A8B8, 255);
            world_rect(r, c, w, h, s->x0, s->y - 1.0f, s->x1, s->y, true);
        } else {
            set_color(r, 0x8890A0, 255);
            world_rect(r, c, w, h, s->x0, s->y - 1.2f, s->x1, s->y, true);
        }
    }

    /* Kämpfer */
    for (int i = 0; i < MAX_FIGHTERS; i++) {
        const Fighter *f = &g->f[i];
        if (f->state == ST_DEAD) continue;
        float hw = f->p.width * 0.5f, hh = f->p.height;
        float squash = (f->state == ST_CROUCH || f->state == ST_JUMPSQUAT || f->state == ST_LANDING) ? 0.7f : 1.0f;
        uint32_t col = f->p.color;
        if (f->state == ST_HITSTUN) col = 0xFFFFFF;
        uint8_t alpha = (f->invuln > 0 && (g->frame / 4) % 2) ? 110 : 255;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        set_color(r, col, alpha);
        world_rect(r, c, w, h, f->pos.x - hw, f->pos.y, f->pos.x + hw, f->pos.y + hh * squash, true);
        /* "Nase" = Blickrichtung */
        set_color(r, 0xFFFFFF, alpha);
        float nx = f->pos.x + (float)f->facing * hw;
        float ny = f->pos.y + hh * squash * 0.75f;
        world_rect(r, c, w, h, fminf(nx, nx + (float)f->facing * 3.0f), ny - 1.5f,
                   fmaxf(nx, nx + (float)f->facing * 3.0f), ny + 1.5f, true);
        /* Spielermarker */
        SDL_FPoint head = w2s(c, w, h, f->pos.x, f->pos.y + hh + 4.0f);
        set_color(r, i == 0 ? 0xFF5050 : 0x50A0FF, 255);
        text_center(r, head.x, head.y - 16, 2, i == 0 ? "P1" : "P2");
        if (debug) {
            set_color(r, 0xC0C0C0, 255);
            text_center(r, head.x, head.y - 28, 1, state_name(f->state));
        }
        Vec2 hc; float hr;
        if (fighter_hitbox(f, &hc, &hr)) {
            set_color(r, 0xFF3030, 255);
            world_circle(r, c, w, h, hc, hr);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* HUD */
    for (int i = 0; i < MAX_FIGHTERS; i++) {
        const Fighter *f = &g->f[i];
        float x = (float)w * (i == 0 ? 0.3f : 0.7f);
        char buf[64];
        set_color(r, f->p.color, 255);
        snprintf(buf, sizeof(buf), "P%d %s", i + 1, f->p.name);
        text_center(r, x, (float)h - 92, 2, buf);
        set_color(r, 0xFFFFFF, 255);
        snprintf(buf, sizeof(buf), "%.0f%%", f->percent);
        text_center(r, x, (float)h - 70, 4, buf);
        char stocks[8] = { 0 };
        for (int s = 0; s < f->stocks && s < 7; s++) stocks[s] = 'o';
        text_center(r, x, (float)h - 30, 2, stocks);
    }
    set_color(r, 0x707070, 255);
    text(r, 10, 10, 1, st->name);
}

/* ---------------- Menü ---------------- */

typedef struct {
    int row;
    CharId chars[2];
    bool p2_cpu;
    int stage;
} Menu;

enum { ROW_P1, ROW_P2, ROW_P2MODE, ROW_STAGE, ROW_START, ROW_COUNT };

static void draw_menu(SDL_Renderer *r, int w, int h, const Menu *m, const Devices *d)
{
    set_color(r, 0x14161C, 255);
    SDL_RenderClear(r);
    set_color(r, 0xFFFFFF, 255);
    text_center(r, (float)w * 0.5f, 60, 4, "PROTO 1v1");
    char buf[96];
    const char *labels[ROW_COUNT];
    char l0[48], l1[48], l2[48], l3[48];
    snprintf(l0, sizeof(l0), "P1 Kaempfer:  < %s >", fighter_params(m->chars[0]).name);
    snprintf(l1, sizeof(l1), "P2 Kaempfer:  < %s >", fighter_params(m->chars[1]).name);
    snprintf(l2, sizeof(l2), "P2 Steuerung: < %s >", m->p2_cpu ? "CPU" : "Mensch");
    snprintf(l3, sizeof(l3), "Stage:        < %d: %s >", m->stage + 1, stage_get(m->stage)->name);
    labels[0] = l0; labels[1] = l1; labels[2] = l2; labels[3] = l3; labels[4] = "START";
    for (int i = 0; i < ROW_COUNT; i++) {
        set_color(r, i == m->row ? 0xFFD040 : 0xB0B0B0, 255);
        text_center(r, (float)w * 0.5f, 180.0f + (float)i * 50.0f, 3, labels[i]);
    }
    set_color(r, 0x808080, 255);
    snprintf(buf, sizeof(buf), "Controller: P1 %s | P2 %s", d->pads[0] ? "Gamepad" : "Tastatur",
             d->pads[1] ? "Gamepad" : "Tastatur");
    text_center(r, (float)w * 0.5f, (float)h - 120, 2, buf);
    text_center(r, (float)w * 0.5f, (float)h - 90, 1.5f,
                "P1: WASD, J=Angriff, Space=Sprung, L=Airdodge, Shift=Gehen | P2: Pfeile, Num1/0/2");
    text_center(r, (float)w * 0.5f, (float)h - 66, 1.5f,
                "GC: A=Angriff, X/Y=Sprung, L/R=Airdodge, Start=Pause | Esc=Menue, F1=Debug");
}

/* ---------------- Screenshot-Modus ---------------- */

static int arg_int(int argc, char **argv, const char *name, int def)
{
    for (int i = 1; i < argc - 1; i++) if (strcmp(argv[i], name) == 0) return atoi(argv[i + 1]);
    return def;
}

static const char *arg_str(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc - 1; i++) if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return NULL;
}

static void draw_result_overlay(SDL_Renderer *r, int w, int h, const Game *g, const char *hint)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "P%d (%s) GEWINNT!", g->winner + 1, g->f[g->winner].p.name);
    set_color(r, 0xFFD040, 255);
    text_center(r, (float)w * 0.5f, (float)h * 0.35f, 4, buf);
    if (hint) {
        set_color(r, 0xFFFFFF, 255);
        text_center(r, (float)w * 0.5f, (float)h * 0.35f + 50, 2, hint);
    }
}

/* Ohne Fenster rendern: Einzelbild (BMP) oder Video (ffmpeg über Pipe). */
static int render_offline(const char *shot, const char *video, const Replay *rp,
                          int stage, int max_frames, CharId c0, CharId c1)
{
    SDL_Surface *surf = SDL_CreateSurface(WIN_W, WIN_H, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "Renderer: %s\n", SDL_GetError()); return 1; }

    Game g;
    CharId cs[2] = { c0, c1 };
    if (rp) { stage = rp->stage; cs[0] = rp->chars[0]; cs[1] = rp->chars[1]; }
    game_init(&g, stage, cs);
    CpuState a, b; cpu_init(&a, 7); cpu_init(&b, 1234);
    Camera cam = { 0 };

    FILE *pipe = NULL;
    if (video) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -loglevel error -y -f rawvideo -pixel_format bgra -video_size %dx%d -framerate 60 -i - "
                 "-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \"%s\"", WIN_W, WIN_H, video);
        pipe = popen(cmd, PIPE_MODE);
        if (!pipe) {
            fprintf(stderr, "ffmpeg konnte nicht gestartet werden (installiert und im PATH?)\n");
            SDL_DestroyRenderer(r);
            SDL_DestroySurface(surf);
            return 1;
        }
    }

    int limit = rp ? rp->nframes : max_frames;
    int tail = 0;                                   /* Frames nach Spielende (Siegerbild) */
    while (g.frame < limit && tail < 120) {
        if (g.over) tail++;
        else {
            Input in[2];
            if (rp) replay_get(rp, g.frame, in);
            else { in[0] = input_quantize(cpu_think(&a, &g, 0)); in[1] = input_quantize(cpu_think(&b, &g, 1)); }
            game_step(&g, in);
        }
        update_camera(&cam, &g, WIN_W, WIN_H, false);
        if (pipe) {
            draw_match(r, WIN_W, WIN_H, &g, &cam, false);
            if (g.over) draw_result_overlay(r, WIN_W, WIN_H, &g, NULL);
            SDL_RenderPresent(r);
            for (int y = 0; y < WIN_H; y++)
                fwrite((const uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch, 4, WIN_W, pipe);
        }
        if (!video && g.over) break;
    }

    int rc = 0;
    if (pipe) {
        rc = pclose(pipe) == 0 ? 0 : 1;
        printf("%s: %s (%d Frames, %s)\n", rc == 0 ? "Video gespeichert" : "ffmpeg-Fehler", video, g.frame,
               g.stage->name);
    }
    if (shot) {
        draw_match(r, WIN_W, WIN_H, &g, &cam, true);
        SDL_RenderPresent(r);
        bool ok = SDL_SaveBMP(surf, shot);
        printf("%s: %s (Frame %d, %s)\n", ok ? "gespeichert" : "Fehler", shot, g.frame, g.stage->name);
        if (!ok) rc = 1;
    }
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(surf);
    return rc;
}

/* Replay unter replays/proto_JJJJMMTT_HHMMSS.p1v1r ablegen */
static bool save_replay(const Replay *rp, char *out, size_t outlen)
{
    if (rp->nframes < 60) return false;
    SDL_CreateDirectory("replays");
    SDL_Time t; SDL_DateTime dt;
    if (!SDL_GetCurrentTime(&t) || !SDL_TimeToDateTime(t, &dt, true)) return false;
    snprintf(out, outlen, "replays/proto_%04d%02d%02d_%02d%02d%02d.p1v1r",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    return replay_save(rp, out);
}

static bool ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* ---------------- Hauptprogramm ---------------- */

typedef enum { SCR_MENU, SCR_MATCH, SCR_PAUSE, SCR_RESULT, SCR_REPLAY } Screen;

int main(int argc, char **argv)
{
    const char *shot = arg_str(argc, argv, "--screenshot");
    const char *video = arg_str(argc, argv, "--video");
    const char *replay_path = arg_str(argc, argv, "--replay");
    if (!replay_path && argc >= 2 && ends_with(argv[1], ".p1v1r")) replay_path = argv[1];

    Replay play = { 0 };
    if (replay_path && !replay_load(&play, replay_path)) {
        fprintf(stderr, "Replay kann nicht gelesen werden: %s\n", replay_path);
        return 1;
    }
    if (shot || video) {
        if (!SDL_Init(0)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
        int rc = render_offline(shot, video, replay_path ? &play : NULL,
                                arg_int(argc, argv, "--stage", 1) - 1,
                                arg_int(argc, argv, "--frames", video ? 60 * 60 * 8 : 600),
                                (CharId)arg_int(argc, argv, "--p1", 0), (CharId)arg_int(argc, argv, "--p2", 1));
        replay_free(&play);
        SDL_Quit();
        return rc;
    }

    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init fehlgeschlagen: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!SDL_CreateWindowAndRenderer("proto1v1", WIN_W, WIN_H, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        SDL_Log("Fenster fehlgeschlagen: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);

    Devices dev = { { NULL, NULL } };
    Menu menu = { ROW_START, { CHAR_FUCHS, CHAR_DUELIST }, true, 0 };
    Screen screen = SCR_MENU;
    Game game;
    Camera cam = { 0 };
    CpuState cpu;
    Input last_nav = { 0 };
    Replay rec = { 0 };
    char saved_msg[160] = "";
    if (replay_path) {
        game_init(&game, play.stage, play.chars);
        screen = SCR_REPLAY;
    }
    bool debug = false;
    bool running = true;
    Uint64 prev = SDL_GetTicksNS();
    double acc = 0.0;
    const double step = 1.0 / 60.0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_GAMEPAD_ADDED:
                for (int i = 0; i < MAX_PADS; i++)
                    if (!dev.pads[i]) { dev.pads[i] = SDL_OpenGamepad(e.gdevice.which); break; }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                for (int i = 0; i < MAX_PADS; i++)
                    if (dev.pads[i] && SDL_GetGamepadID(dev.pads[i]) == e.gdevice.which) {
                        SDL_CloseGamepad(dev.pads[i]);
                        dev.pads[i] = NULL;
                    }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.repeat) break;
                if (e.key.scancode == SDL_SCANCODE_F1) debug = !debug;
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    if (screen == SCR_MENU || screen == SCR_REPLAY) running = false;
                    else {
                        if (screen == SCR_MATCH || screen == SCR_PAUSE) {
                            char path[128];
                            if (save_replay(&rec, path, sizeof(path))) SDL_Log("Replay gespeichert: %s", path);
                        }
                        screen = SCR_MENU;
                    }
                }
                break;
            default: break;
            }
        }

        Uint64 now = SDL_GetTicksNS();
        acc += (double)(now - prev) / 1e9;
        prev = now;
        if (acc > 0.25) acc = 0.25;

        int w, h;
        SDL_GetRenderOutputSize(ren, &w, &h);

        while (acc >= step) {
            acc -= step;
            Input p[2] = { read_player(&dev, 0), read_player(&dev, 1) };
            /* Menü/Pause mit Flanken aus beiden Spielern */
            Input nav = p[0];
            if (fabsf(p[1].stick_x) > fabsf(nav.stick_x)) nav.stick_x = p[1].stick_x;
            if (fabsf(p[1].stick_y) > fabsf(nav.stick_y)) nav.stick_y = p[1].stick_y;
            nav.a |= p[1].a; nav.b |= p[1].b; nav.start |= p[1].start; nav.jump |= p[1].jump;
            bool up = nav.stick_y > 0.6f && last_nav.stick_y <= 0.6f;
            bool down = nav.stick_y < -0.6f && last_nav.stick_y >= -0.6f;
            bool left = nav.stick_x < -0.6f && last_nav.stick_x >= -0.6f;
            bool right = nav.stick_x > 0.6f && last_nav.stick_x <= 0.6f;
            bool ok = (nav.a && !last_nav.a) || (nav.start && !last_nav.start);
            bool start = nav.start && !last_nav.start;
            bool back = nav.b && !last_nav.b;
            last_nav = nav;

            switch (screen) {
            case SCR_MENU: {
                if (up) menu.row = (menu.row + ROW_COUNT - 1) % ROW_COUNT;
                if (down) menu.row = (menu.row + 1) % ROW_COUNT;
                int d = right ? 1 : (left ? -1 : 0);
                if (d) {
                    if (menu.row == ROW_P1) menu.chars[0] = (CharId)((menu.chars[0] + NUM_CHARS + d) % NUM_CHARS);
                    if (menu.row == ROW_P2) menu.chars[1] = (CharId)((menu.chars[1] + NUM_CHARS + d) % NUM_CHARS);
                    if (menu.row == ROW_P2MODE) menu.p2_cpu = !menu.p2_cpu;
                    if (menu.row == ROW_STAGE) menu.stage = (menu.stage + NUM_STAGES + d) % NUM_STAGES;
                }
                if (ok && (menu.row == ROW_START || start)) {
                    game_init(&game, menu.stage, menu.chars);
                    replay_free(&rec);
                    replay_init(&rec, menu.stage, menu.chars);
                    saved_msg[0] = 0;
                    cpu_init(&cpu, (uint32_t)SDL_GetTicks() | 1u);
                    cam.init = false;
                    screen = SCR_MATCH;
                }
                break;
            }
            case SCR_MATCH:
                if (start) { screen = SCR_PAUSE; break; }
                if (menu.p2_cpu) p[1] = cpu_think(&cpu, &game, 1);
                p[0].start = p[1].start = false;
                p[0] = input_quantize(p[0]);
                p[1] = input_quantize(p[1]);
                replay_push(&rec, p);
                game_step(&game, p);
                update_camera(&cam, &game, w, h, false);
                if (game.over) {
                    char path[128];
                    if (save_replay(&rec, path, sizeof(path)))
                        snprintf(saved_msg, sizeof(saved_msg), "Replay: %s", path);
                    screen = SCR_RESULT;
                }
                break;
            case SCR_REPLAY:
                replay_get(&play, game.frame, p);
                game_step(&game, p);
                update_camera(&cam, &game, w, h, false);
                if (game.over || game.frame >= play.nframes) screen = SCR_RESULT;
                break;
            case SCR_PAUSE:
                if (start) screen = SCR_MATCH;
                else if (back) {
                    char path[128];
                    if (save_replay(&rec, path, sizeof(path))) SDL_Log("Replay gespeichert: %s", path);
                    screen = SCR_MENU;
                }
                break;
            case SCR_RESULT:
                if (ok) screen = SCR_MENU;
                break;
            }
        }

        if (screen == SCR_MENU) {
            draw_menu(ren, w, h, &menu, &dev);
        } else {
            draw_match(ren, w, h, &game, &cam, debug);
            if (screen == SCR_PAUSE) {
                set_color(ren, 0xFFFFFF, 255);
                text_center(ren, (float)w * 0.5f, (float)h * 0.35f, 5, "PAUSE");
                text_center(ren, (float)w * 0.5f, (float)h * 0.35f + 60, 2, "Start = weiter, B/Esc = Menue");
            } else if (screen == SCR_REPLAY) {
                set_color(ren, 0xFF5050, 255);
                text(ren, (float)w - 120, 10, 2, "REPLAY");
            } else if (screen == SCR_RESULT) {
                if (game.winner >= 0) draw_result_overlay(ren, w, h, &game, "A / Start = Menue");
                set_color(ren, 0xA0A0A0, 255);
                if (saved_msg[0]) text_center(ren, (float)w * 0.5f, (float)h * 0.35f + 80, 1.5f, saved_msg);
            }
        }
        SDL_RenderPresent(ren);
    }

    replay_free(&rec);
    replay_free(&play);
    for (int i = 0; i < MAX_PADS; i++) if (dev.pads[i]) SDL_CloseGamepad(dev.pads[i]);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
