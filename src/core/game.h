/* Kern-Simulation: plattformunabhängig, ohne SDL, fester 60-Hz-Takt.
 * Koordinaten: x nach rechts, y nach oben. pos = Fußpunkt des Kämpfers. */
#ifndef PROTO_GAME_H
#define PROTO_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_FIGHTERS 2
#define MAX_SURFACES 8
#define NUM_STAGES 5
#define START_STOCKS 4

typedef struct { float x, y; } Vec2;

/* Abstrakter Controller-Zustand eines Frames (GC-Layout). */
typedef struct {
    float stick_x, stick_y;   /* -1..1, y nach oben */
    float cstick_x, cstick_y;
    bool a, b, jump, shield, z, start;
} Input;

typedef enum { SURF_SOLID, SURF_PLATFORM } SurfKind;

/* SOLID: Block von y (Oberkante) bis bottom, Kanten links/rechts sind greifbar.
 * PLATFORM: einseitig durchlässige Plattform auf Höhe y. */
typedef struct {
    SurfKind kind;
    float x0, x1, y, bottom;
} Surface;

typedef struct {
    const char *name;
    Surface surf[MAX_SURFACES];
    int nsurf;
    float blast_left, blast_right, blast_top, blast_bottom;
    Vec2 spawn[MAX_FIGHTERS];
} Stage;

typedef enum {
    ST_IDLE, ST_WALK, ST_DASH, ST_RUN, ST_TURN, ST_CROUCH,
    ST_JUMPSQUAT, ST_AIR, ST_FREEFALL, ST_AIRDODGE, ST_LANDING,
    ST_LEDGE, ST_LEDGE_GETUP, ST_ATTACK, ST_HITSTUN, ST_DEAD,
    ST_COUNT
} FState;

typedef enum { ATK_NONE, ATK_JAB, ATK_FSTRONG, ATK_NAIR, ATK_COUNT } AttackId;

typedef struct {
    const char *name;
    int total, active_start, active_end;  /* Frames, ab 0 gezählt */
    float off_x, off_y, radius;            /* Hitbox relativ zum Fußpunkt */
    float damage, base_kb, kb_growth, angle_deg;
    int landing_lag;                        /* nur Luftangriffe */
    bool aerial;
} AttackData;

typedef enum { CHAR_FUCHS, CHAR_BIRDIE, CHAR_ASSASSIN, CHAR_DUELIST, NUM_CHARS } CharId;

typedef struct {
    const char *name;
    float walk_max, walk_accel, dash_init, run_max, ground_friction;
    float air_accel_base, air_accel_add, air_max, air_friction;
    float gravity, fall_max, fastfall;
    int jumpsquat;
    float fullhop, shorthop, djump;   /* Anfangsgeschwindigkeiten (aus Höhen berechnet) */
    int max_jumps;
    float airdodge_speed;
    int airdodge_frames, land_lag, wavedash_lag;
    float width, height, weight;      /* weight im Melee-Maßstab (100 = Mario) */
    uint32_t color;                    /* 0xRRGGBB für den Platzhalter */
} FighterParams;

typedef struct {
    FighterParams p;
    CharId character;
    FState state;
    int state_frame;
    Vec2 pos, vel, kb;          /* kb = Knockback-Geschwindigkeit (zerfällt) */
    int facing;                 /* +1 rechts, -1 links */
    bool grounded;
    int surface;                /* Index der Bodenfläche oder -1 */
    int jumps_left;
    bool fastfalling;
    bool full_hop;              /* Sprungtaste in Jumpsquat gehalten */
    bool buffered_airdodge;
    AttackId attack;
    bool attack_hit;
    int landing_lag;
    int hitstun;
    float percent;
    int stocks;
    int invuln;
    int ledge;                  /* surf*2 + (0 links / 1 rechts), -1 = keine */
    int ledge_cooldown;
    int drop_through;
    Input in, prev;
    float stick_x_hist[3];      /* für Smash-Erkennung */
} Fighter;

typedef struct {
    const Stage *stage;
    int stage_index;
    Fighter f[MAX_FIGHTERS];
    int frame;
    bool over;
    int winner;                 /* -1 = keiner */
} Game;

/* stage.c */
const Stage *stage_get(int index);

/* fighter.c */
const AttackData *attack_data(AttackId id);
const char *state_name(FState s);
FighterParams fighter_params(CharId c);
bool fighter_hitbox(const Fighter *f, Vec2 *center, float *radius);

/* game.c */
void game_init(Game *g, int stage_index, const CharId chars[MAX_FIGHTERS]);
void game_step(Game *g, const Input inputs[MAX_FIGHTERS]);

/* cpu.c – einfacher Bot, erzeugt Input für Spieler `self` */
typedef struct { uint32_t rng; int timer; int press_phase; } CpuState;
void cpu_init(CpuState *c, uint32_t seed);
Input cpu_think(CpuState *c, const Game *g, int self);

#endif
