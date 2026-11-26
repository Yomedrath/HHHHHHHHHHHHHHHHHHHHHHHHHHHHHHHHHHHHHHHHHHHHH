/* Headless-Tests der Kern-Simulation (ohne Grafik). */
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FEHLER: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const Input NONE = { 0 };

static void run(Game *g, Input p0, Input p1, int frames)
{
    Input in[2] = { p0, p1 };
    for (int i = 0; i < frames; i++) game_step(g, in);
}

static void settle(Game *g) { run(g, NONE, NONE, 120); }

static void test_landing(int stage, CharId c)
{
    Game g; CharId cs[2] = { c, c };
    game_init(&g, stage, cs);
    settle(&g);
    CHECK(g.f[0].grounded && g.f[1].grounded, "Stage %s/%s: beide sollten stehen (state %s)",
          g.stage->name, fighter_params(c).name, state_name(g.f[0].state));
}

static float max_height(Game *g, Input press, int hold_frames)
{
    float y0 = g->f[0].pos.y, top = y0;
    Input in[2] = { press, NONE };
    for (int i = 0; i < 120; i++) {
        if (i == hold_frames) in[0] = NONE;
        game_step(g, in);
        if (g->f[0].pos.y > top) top = g->f[0].pos.y;
        if (i > 5 && g->f[0].grounded) break;
    }
    return top - y0;
}

static void test_jumps(CharId c, float expect_full, float expect_short)
{
    Game g; CharId cs[2] = { c, c };
    Input jump = { 0 }; jump.jump = true;
    game_init(&g, 1, cs); settle(&g);
    float full = max_height(&g, jump, 30);
    game_init(&g, 1, cs); settle(&g);
    float sh = max_height(&g, jump, 1);
    printf("  %-9s Full Hop %.2f (Melee %.2f) | Short Hop %.2f (Melee %.2f)\n",
           fighter_params(c).name, full, expect_full, sh, expect_short);
    CHECK(fabsf(full - expect_full) < 1.5f, "%s Full Hop weicht ab", fighter_params(c).name);
    CHECK(fabsf(sh - expect_short) < 1.5f, "%s Short Hop weicht ab", fighter_params(c).name);
}

static void test_wavedash(void)
{
    Game g; CharId cs[2] = { CHAR_FUCHS, CHAR_BIRDIE };
    game_init(&g, 1, cs); settle(&g);
    float x0 = g.f[0].pos.x;
    Input jump = { 0 }; jump.jump = true;
    Input dodge = { 0 }; dodge.shield = true; dodge.stick_x = 0.95f; dodge.stick_y = -0.3f;
    run(&g, jump, NONE, 1);
    Input hold = { 0 };
    for (int i = 0; i < 10 && g.f[0].state == ST_JUMPSQUAT; i++) run(&g, hold, NONE, 1);
    run(&g, dodge, NONE, 1);
    int guard = 0;
    while (!g.f[0].grounded && guard++ < 40) run(&g, NONE, NONE, 1);
    FState s = g.f[0].state;
    float vx = g.f[0].vel.x;
    run(&g, NONE, NONE, 60);
    float dist = g.f[0].pos.x - x0;
    printf("  Wavedash Fuchs: Zustand nach Landung %s, vx %.2f, Strecke %.1f\n", state_name(s), vx, dist);
    CHECK(s == ST_LANDING && vx > 1.0f && dist > 15.0f, "Wavedash rutscht nicht");
}

/* Fighter hoch in die Luft setzen und einen Airdodge mit gegebenem Stick auslösen */
static Fighter *dodge_setup(Game *g, float sx, float sy)
{
    CharId cs[2] = { CHAR_FUCHS, CHAR_BIRDIE };
    game_init(g, 1, cs);
    Fighter *f = &g->f[0];
    f->pos.x = 0; f->pos.y = 100; f->vel.x = 1.0f; f->vel.y = 2.0f;
    Input d = { 0 }; d.shield = true; d.stick_x = sx; d.stick_y = sy;
    run(g, d, NONE, 1);
    return f;
}

static void test_airdodge(void)
{
    Game g;
    /* neutral: Bewegung wird gestoppt, Schweben ohne Schwerkraft bis Frame 29 */
    Fighter *f = dodge_setup(&g, 0, 0);
    float y0 = f->pos.y;
    CHECK(f->state == ST_AIRDODGE && f->vel.x == 0 && f->vel.y == 0, "Neutral-Airdodge stoppt Momentum nicht");
    run(&g, NONE, NONE, 27);
    CHECK(fabsf(f->pos.y - y0) < 0.01f, "Neutral-Airdodge schwebt nicht (dy %.2f)", f->pos.y - y0);
    run(&g, NONE, NONE, 3);
    CHECK(f->pos.y < y0, "nach Frame 29 setzt Schwerkraft nicht ein");

    /* Deadzone: kleiner Stick -> auf der Stelle */
    f = dodge_setup(&g, 0.2f, 0.2f);
    CHECK(f->vel.x == 0 && f->vel.y == 0, "Deadzone ignoriert");

    /* Richtung: halber Stick nach rechts = volle Kraft (nur der Winkel zählt) */
    f = dodge_setup(&g, 0.5f, 0.0f);
    float x0 = 0;                                  /* Startpunkt vor dem Airdodge-Frame */
    run(&g, NONE, NONE, 40);
    float expect = 0;
    for (int i = 1; i <= 29; i++) expect += 3.1f * powf(0.9f, (float)i);
    printf("  Airdodge rechts: Strecke %.2f (Melee-Modell %.2f + Restdrift), Zustand %s\n", f->pos.x - x0, expect,
           state_name(f->state));
    CHECK(fabsf((f->pos.x - x0) - expect) < 2.0f, "Airdodge-Strecke weicht ab");

    /* Unverwundbarkeit Frame 4..29, Länge 49, danach Freefall */
    f = dodge_setup(&g, 0, 0);
    int first = -1, last = -1, frame = 1;
    while (f->state == ST_AIRDODGE && frame < 80) {
        if (fighter_intangible(f)) { if (first < 0) first = frame; last = frame; }
        run(&g, NONE, NONE, 1);
        frame++;
    }
    printf("  Airdodge: unverwundbar %d-%d, Freefall ab Frame %d\n", first, last, frame);
    CHECK(first == 4 && last == 29 && frame == 50 && f->state == ST_FREEFALL,
          "Airdodge-Timing falsch (%d-%d, Ende %d)", first, last, frame);
}

static void test_replay(void)
{
    Game a, b; CharId cs[2] = { CHAR_ASSASSIN, CHAR_DUELIST };
    game_init(&a, 5, cs);
    CpuState c0, c1; cpu_init(&c0, 5); cpu_init(&c1, 6);
    Replay r; replay_init(&r, 5, cs);
    while (!a.over && a.frame < 60 * 60 * 8) {
        Input in[2] = { input_quantize(cpu_think(&c0, &a, 0)), input_quantize(cpu_think(&c1, &a, 1)) };
        replay_push(&r, in);
        game_step(&a, in);
    }
    CHECK(replay_save(&r, "test_replay.p1v1r"), "Replay speichern fehlgeschlagen");
    Replay l;
    CHECK(replay_load(&l, "test_replay.p1v1r"), "Replay laden fehlgeschlagen");
    game_init(&b, l.stage, l.chars);
    for (int i = 0; i < l.nframes; i++) { Input in[2]; replay_get(&l, i, in); game_step(&b, in); }
    bool same = a.frame == b.frame && a.winner == b.winner &&
                a.f[0].pos.x == b.f[0].pos.x && a.f[1].pos.y == b.f[1].pos.y &&
                a.f[0].percent == b.f[0].percent;
    printf("  Replay: %d Frames, identisch: %s\n", l.nframes, same ? "ja" : "NEIN");
    CHECK(same, "Replay nicht deterministisch");
    replay_free(&r); replay_free(&l);
    remove("test_replay.p1v1r");
}

static void test_ledge(void)
{
    Game g; CharId cs[2] = { CHAR_DUELIST, CHAR_BIRDIE };
    game_init(&g, 0, cs);
    Fighter *f = &g.f[0];
    f->pos.x = g.stage->surf[0].x1 + 6.0f;     /* rechts neben der Kante */
    f->pos.y = -4.0f;
    f->vel.y = -1.0f;
    for (int i = 0; i < 30 && f->state != ST_LEDGE; i++) run(&g, NONE, NONE, 1);
    CHECK(f->state == ST_LEDGE, "Ledge wurde nicht gegriffen (state %s)", state_name(f->state));
    Input up = { 0 }; up.stick_y = 1.0f;
    run(&g, NONE, NONE, 10);
    run(&g, up, NONE, 1);
    run(&g, NONE, NONE, 40);
    CHECK(f->grounded && f->state == ST_IDLE, "Getup endet nicht auf der Stage (state %s)", state_name(f->state));
}

static void test_hit(void)
{
    Game g; CharId cs[2] = { CHAR_BIRDIE, CHAR_FUCHS };
    game_init(&g, 1, cs); settle(&g);
    g.f[0].pos.x = 0; g.f[1].pos.x = 9; g.f[0].facing = 1;
    Input a = { 0 }; a.a = true; a.stick_x = 1.0f;
    run(&g, a, NONE, 1);
    run(&g, NONE, NONE, 20);
    printf("  Strong-Treffer: Fuchs %.0f%%, Zustand %s\n", g.f[1].percent, state_name(g.f[1].state));
    CHECK(g.f[1].percent > 0, "Angriff trifft nicht");
}

static void test_cpu_match(void)
{
    for (int st = 0; st < NUM_STAGES; st++) {
        Game g; CharId cs[2] = { (CharId)(st % NUM_CHARS), (CharId)((st + 1) % NUM_CHARS) };
        game_init(&g, st, cs);
        CpuState c0, c1; cpu_init(&c0, 1 + st); cpu_init(&c1, 99 + st);
        int frames = 0;
        bool sane = true;
        while (!g.over && frames < 60 * 60 * 8) {
            Input in[2] = { cpu_think(&c0, &g, 0), cpu_think(&c1, &g, 1) };
            game_step(&g, in);
            frames++;
            for (int i = 0; i < 2; i++)
                if (!isfinite(g.f[i].pos.x) || !isfinite(g.f[i].pos.y)) sane = false;
        }
        printf("  CPU vs CPU auf %-9s (%s vs %s): %s nach %d s, Stocks %d:%d\n", g.stage->name,
               g.f[0].p.name, g.f[1].p.name, g.over ? "Sieg" : "kein Sieger", frames / 60,
               g.f[0].stocks, g.f[1].stocks);
        CHECK(sane, "NaN in Positionen");
    }
}

int main(void)
{
    printf("Landung auf allen Stages/Kämpfern\n");
    for (int s = 0; s < NUM_STAGES; s++)
        for (int c = 0; c < NUM_CHARS; c++) test_landing(s, (CharId)c);
    printf("Sprunghöhen vs. Melee-Werte\n");
    test_jumps(CHAR_FUCHS, 31.28f, 10.65f);
    test_jumps(CHAR_BIRDIE, 38.52f, 14.85f);
    test_jumps(CHAR_ASSASSIN, 34.08f, 20.16f);
    test_jumps(CHAR_DUELIST, 35.09f, 13.995f);
    printf("Techniken\n");
    test_wavedash();
    test_airdodge();
    test_replay();
    test_ledge();
    test_hit();
    printf("Autonome Matches\n");
    test_cpu_match();
    printf(failures ? "\n%d Fehler\n" : "\nAlle Tests bestanden\n", failures);
    return failures ? 1 : 0;
}
