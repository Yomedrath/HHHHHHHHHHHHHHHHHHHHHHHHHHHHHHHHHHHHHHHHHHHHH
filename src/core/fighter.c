/* Platzhalter-Kämpfer: eigene Werte, frei gewählt. */
#include "game.h"
#include <math.h>

static const AttackData ATTACKS[ATK_COUNT] = {
    [ATK_NONE]    = { "none",    0,  0,  0,  0, 0, 0,  0,  0,     0,     0, 0, false },
    [ATK_JAB]     = { "jab",    16,  3,  5,  7, 8, 5,  4, 1.2f, 0.012f, 35, 0, false },
    [ATK_FSTRONG] = { "fstrong",34, 10, 13, 10, 8, 6, 13, 2.0f, 0.035f, 40, 0, false },
    [ATK_NAIR]    = { "nair",   30,  4, 18,  0, 7, 8,  9, 1.6f, 0.022f, 45, 8, true  },
};

static const char *STATE_NAMES[ST_COUNT] = {
    "Idle", "Walk", "Dash", "Run", "Turn", "Crouch",
    "JumpSquat", "Air", "Freefall", "Airdodge", "Landing",
    "Ledge", "LedgeGetup", "Attack", "Hitstun", "Dead",
};

const AttackData *attack_data(AttackId id)
{
    if (id < 0 || id >= ATK_COUNT) id = ATK_NONE;
    return &ATTACKS[id];
}

const char *state_name(FState s)
{
    return (s >= 0 && s < ST_COUNT) ? STATE_NAMES[s] : "?";
}

/* Bewegungswerte nach öffentlich dokumentierten Melee-Attributen (SmashWiki, NTSC):
 * Fuchs = Fox, Birdie = Captain Falcon, Assassin = Sheik, Duelist = Marth.
 * Sprunghöhen in Einheiten; Startgeschwindigkeit wird daraus berechnet.
 * walk_accel, width/height und Farben sind Platzhalter (nicht dokumentiert/geschätzt). */
typedef struct {
    const char *name;
    float weight, dash_init, run, walk, traction, air_friction, air_speed;
    float accel_base, accel_add, gravity, fall, fastfall;
    int jumpsquat;
    float jump_h, shorthop_h, djump_h;
    int land_lag;
    float width, height;
    uint32_t color;
} CharRow;

static const CharRow CHARS[NUM_CHARS] = {
    /*               wt  dash  run  walk  trac  airfr  airsp  acc_b acc_a  grav   fall ff   js  jump   SH      DJ     ll  w    h     color */
    { "Fuchs",       75, 1.9f, 2.2f, 1.6f, 0.08f, 0.02f,  0.83f, 0.02f, 0.06f, 0.23f,  2.8f, 3.4f, 3, 31.28f, 10.65f, 40.204f, 4, 6.5f, 12.0f, 0xE08A2Eu },
    { "Birdie",     104, 2.0f, 2.3f, 0.85f,0.08f, 0.01f,  1.12f, 0.02f, 0.04f, 0.13f,  2.9f, 3.5f, 4, 38.52f, 14.85f, 28.56f,  4, 8.0f, 16.0f, 0x3355DD },
    { "Assassin",    90, 1.7f, 1.8f, 1.2f, 0.08f, 0.04f,  0.80f, 0.02f, 0.04f, 0.12f,  2.13f,3.0f, 3, 34.08f, 20.16f, 38.0f,   4, 6.5f, 15.0f, 0x7A4FB5 },
    { "Duelist",     87, 1.5f, 1.8f, 1.6f, 0.06f, 0.005f, 0.90f, 0.02f, 0.03f, 0.085f, 2.2f, 2.5f, 4, 35.09f, 13.995f,25.188f, 4, 7.0f, 16.0f, 0x2E9E6B },
};

/* Höhe h bei diskreter Integration (erst Position, dann Schwerkraft): h ≈ v²/(2g) + v/2 */
static float jump_velocity(float h, float g)
{
    return (-g + sqrtf(g * g + 8.0f * g * h)) * 0.5f;
}

FighterParams fighter_params(CharId c)
{
    if (c < 0 || c >= NUM_CHARS) c = CHAR_FUCHS;
    const CharRow *r = &CHARS[c];
    FighterParams p;
    p.name = r->name;
    p.walk_max = r->walk;
    p.walk_accel = 0.2f;
    p.dash_init = r->dash_init;
    p.run_max = r->run;
    p.ground_friction = r->traction;
    p.air_accel_base = r->accel_base;
    p.air_accel_add = r->accel_add;
    p.air_max = r->air_speed;
    p.air_friction = r->air_friction;
    p.gravity = r->gravity;
    p.fall_max = r->fall;
    p.fastfall = r->fastfall;
    p.jumpsquat = r->jumpsquat;
    p.fullhop = jump_velocity(r->jump_h, r->gravity);
    p.shorthop = jump_velocity(r->shorthop_h, r->gravity);
    p.djump = jump_velocity(r->djump_h, r->gravity);
    p.max_jumps = 2;
    p.land_lag = r->land_lag;
    p.width = r->width;
    p.height = r->height;
    p.weight = r->weight;
    p.color = r->color;
    return p;
}

bool fighter_hitbox(const Fighter *f, Vec2 *center, float *radius)
{
    if (f->state != ST_ATTACK) return false;
    const AttackData *a = attack_data(f->attack);
    if (f->state_frame < a->active_start || f->state_frame > a->active_end) return false;
    center->x = f->pos.x + a->off_x * (float)f->facing;
    center->y = f->pos.y + a->off_y;
    *radius = a->radius;
    return true;
}
