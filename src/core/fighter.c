/* Platzhalter-Kämpfer: eigene Werte, frei gewählt. */
#include "game.h"
#include <math.h>

/* Bodenangriffe: Platzhalter, für alle gleich */
#define W1(a, b) 1, { { a, b } }
static const AttackData GROUND[3] = {
    [ATK_NONE]    = { "none",     0, 0, { { 0, 0 } }, 0, 0, 0, 0,  0, 0, 0,  0,    0,      0,  0, 0, false },
    [ATK_JAB]     = { "jab",     16, W1(4, 6),        0, 0, 0, 0,  7, 8, 5,  4, 1.2f, 0.012f, 35, 1, false },
    [ATK_FSTRONG] = { "fstrong", 34, W1(11, 14),      0, 0, 0, 0, 10, 8, 6, 13, 2.0f, 0.035f, 40, 1, false },
};

/* Aerials: Frames, aktive Fenster, IASA, Autocancel und Landing Lag nach SmashWiki (SSBM, NTSC),
 * Landing Lag zusätzlich mit Slippi-Replays abgeglichen. Hitbox-Position/-Größe, Knockback: Platzhalter.
 * Spalten: Name, Länge, Fenster, IASA, AC früh, AC spät, Lag, off_x, off_y, r, Schaden, BKB, KBG, Winkel, Richtung */
#define AER(n, t, nw, w, ia, e, l, lag, ox, oy, r, dmg, ang, dir) \
    { n, t, nw, w, ia, e, l, lag, ox, oy, r, dmg, 0.6f + dmg * 0.08f, 0.0015f * dmg, ang, dir, true }
#define WIN(...) { __VA_ARGS__ }

static const AttackData AERIALS[NUM_CHARS][5] = {
    [CHAR_FUCHS] = {
        AER("nair", 49, 1, WIN({ 4, 31 }),                                         42, 3, 37, 15,  0, 7, 7, 12, 45, 1),
        AER("fair", 59, 5, WIN({ 6, 8 }, { 16, 18 }, { 24, 26 }, { 33, 35 }, { 43, 45 }), 53, 5, 49, 22, 6, 7, 6,  7, 45, 1),
        AER("bair", 39, 1, WIN({ 4, 19 }),                                         38, 3, 23, 20,  7, 7, 6, 15, 35, -1),
        AER("uair", 39, 2, WIN({ 8, 9 }, { 11, 14 }),                              36, 7, 26, 18,  0, 15, 6, 13, 85, 1),
        AER("dair", 49, 7, WIN({ 5, 6 }, { 8, 9 }, { 11, 12 }, { 14, 15 }, { 17, 18 }, { 20, 21 }, { 23, 24 }),
                                                                                    0, 4, 31, 18,  0, -1, 6,  3, 290, 1),
    },
    [CHAR_BLEIFUSS] = {
        AER("nair", 44, 2, WIN({ 7, 12 }, { 20, 29 }),                              0, 3, 34, 15,  0, 8, 8,  7, 45, 1),
        AER("fair", 39, 1, WIN({ 14, 30 }),                                        36, 6, 35, 19,  8, 9, 6, 18, 40, 1),
        AER("bair", 35, 1, WIN({ 10, 17 }),                                        29, 6, 21, 18,  8, 9, 6, 14, 35, -1),
        AER("uair", 33, 1, WIN({ 6, 13 }),                                         30, 0, 22, 15,  0, 18, 7, 13, 85, 1),
        AER("dair", 44, 1, WIN({ 16, 20 }),                                        38, 3, 36, 24,  0, -1, 7, 16, 270, 1),
    },
    [CHAR_ASSASSINE] = {
        AER("nair", 48, 1, WIN({ 3, 30 }),                                         42, 2, 31, 16,  0, 8, 7, 14, 45, 1),
        AER("fair", 33, 1, WIN({ 5, 7 }),                                           0, 4, 11, 16,  7, 9, 6, 13, 40, 1),
        AER("bair", 38, 1, WIN({ 4, 19 }),                                          0, 3, 24, 16,  7, 8, 6, 14, 35, -1),
        AER("uair", 39, 1, WIN({ 5, 20 }),                                         37, 4, 30, 24,  0, 17, 6, 12, 85, 1),
        AER("dair", 48, 1, WIN({ 15, 33 }),                                         0, 2, 53, 20,  0, -1, 6, 11, 270, 1),
    },
    [CHAR_BIRDIE] = {   /* Falco */
        AER("nair", 49, 1, WIN({ 4, 31 }),                                         42, 3, 37, 15,  0, 7, 7, 12, 45, 1),
        AER("fair", 59, 5, WIN({ 6, 8 }, { 16, 18 }, { 24, 26 }, { 33, 35 }, { 43, 45 }), 53, 5, 49, 22, 6, 7, 6,  9, 45, 1),
        AER("bair", 39, 1, WIN({ 4, 19 }),                                         38, 3, 23, 20,  7, 7, 6, 15, 35, -1),
        AER("uair", 39, 2, WIN({ 8, 9 }, { 11, 14 }),                              36, 7, 26, 18,  0, 15, 6, 10, 85, 1),
        AER("dair", 49, 1, WIN({ 5, 24 }),                                          0, 4, 30, 18,  0, -1, 6, 12, 270, 1),
    },
    [CHAR_DUELIST] = {
        AER("nair", 49, 2, WIN({ 6, 7 }, { 15, 21 }),                               0, 5, 25, 15,  0, 9, 10, 10, 45, 1),
        AER("fair", 33, 1, WIN({ 4, 7 }),                                          30, 0, 27, 15, 10, 10, 9, 10, 40, 1),
        AER("bair", 39, 1, WIN({ 7, 11 }),                                         35, 0, 32, 24, 10, 10, 9, 13, 35, -1),
        AER("uair", 45, 1, WIN({ 5, 8 }),                                           0, 4, 27, 15,  0, 20, 9, 13, 85, 1),
        AER("dair", 59, 1, WIN({ 6, 9 }),                                           0, 5, 48, 32,  0, -2, 9, 13, 270, 1),
    },
};

static const char *STATE_NAMES[ST_COUNT] = {
    "Idle", "Walk", "Dash", "Run", "Turn", "Crouch",
    "JumpSquat", "Air", "Freefall", "Airdodge", "Landing",
    "Ledge", "LedgeGetup", "Attack", "Hitstun", "Dead",
};

const AttackData *attack_data(CharId c, AttackId id)
{
    if (c < 0 || c >= NUM_CHARS) c = CHAR_FUCHS;
    if (id >= ATK_NAIR && id <= ATK_DAIR) return &AERIALS[c][id - ATK_NAIR];
    if (id < 0 || id > ATK_FSTRONG) id = ATK_NONE;
    return &GROUND[id];
}

const char *state_name(FState s)
{
    return (s >= 0 && s < ST_COUNT) ? STATE_NAMES[s] : "?";
}

/* Bewegungswerte nach öffentlich dokumentierten Melee-Attributen (SmashWiki, NTSC):
 * Fuchs = Fox, Bleifuss = Captain Falcon, Assassine = Sheik, Duelist = Marth, Birdie = Falco.
 * Sprunghöhen in Einheiten; Startgeschwindigkeit wird daraus berechnet.
 * walk_accel, width/height und Farben sind Platzhalter (nicht dokumentiert/geschätzt). */
typedef struct {
    const char *name;
    float weight, dash_init, run, walk, traction, air_friction, air_speed;
    float accel_base, accel_add, gravity, fall, fastfall;
    int jumpsquat;
    float fullhop_v, shorthop_v, djump_v;   /* Startgeschwindigkeiten, aus Slippi-Replays gemessen */
    int land_lag;
    float width, height;
    uint32_t color;
} CharRow;

/* Quellen: SmashWiki-Attribute; mit tools/measure_attributes.py gegen 22 Slippi-Replays geprüft
 * (Gravitation, Fall/Fastfall, Jumpsquat, Sprung-Startgeschwindigkeiten, Traction, Luftreibung,
 * Dash, Landing Lag stimmen exakt). Sprung- und Dash-Werte sind direkt gemessen. */
static const CharRow CHARS[NUM_CHARS] = {
    /*               wt  dash   run  walk  trac  airfr  airsp  acc_b acc_a  grav   fall ff   js  FH-v  SH-v   DJ-v    ll  w    h     color */
    { "Fuchs",       75, 1.9f,  2.2f, 1.6f, 0.08f, 0.02f,  0.83f, 0.02f, 0.06f, 0.23f,  2.8f, 3.4f, 3, 3.68f, 2.1f,  4.186f, 4, 6.5f, 12.0f, 0xE08A2Eu },
    { "Bleifuss",   104, 2.0f,  2.3f, 0.85f,0.08f, 0.01f,  1.12f, 0.02f, 0.04f, 0.13f,  2.9f, 3.5f, 4, 3.1f,  1.9f,  2.66f,  4, 8.0f, 16.0f, 0xC0392B },
    { "Assassine",   90, 1.68f, 1.8f, 1.2f, 0.08f, 0.04f,  0.80f, 0.02f, 0.04f, 0.12f,  2.13f,3.0f, 3, 2.8f,  2.14f, 2.96f,  4, 6.5f, 15.0f, 0x7A4FB5 },
    { "Duelist",     87, 1.5f,  1.8f, 1.6f, 0.06f, 0.005f, 0.90f, 0.02f, 0.03f, 0.085f, 2.2f, 2.5f, 4, 2.4f,  1.5f,  2.027f, 4, 7.0f, 16.0f, 0x2E9E6B },
    /* Falco: Dash 1.70 = gemessene Frame-2-Geschwindigkeit 1.82 minus Beschleunigung 0.1188 */
    { "Birdie",      80, 1.70f, 1.5f, 1.4f, 0.08f, 0.02f,  0.83f, 0.02f, 0.05f, 0.17f,  3.1f, 3.5f, 5, 4.1f,  1.9f,  3.684f, 4, 6.5f, 13.0f, 0x4A90E2 },
};

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
    /* Dash-Beschleunigung bei vollem Stick, aus Slippi-Replays gemessen (Geschwindigkeit im 2. Dash-Frame
     * minus Startgeschwindigkeit, n = 88-2957 Messungen): Fox 0.12, Sheik 0.12, Falcon 0.158, Marth 0.0592.
     * Aufteilung Basis/Stickanteil 1:5 wie bei Fox (0.02 + 0.1 * |Stick|). */
    static const float DASH_ACC_FULL[NUM_CHARS] = { 0.12f, 0.158f, 0.12f, 0.0592f, 0.1188f };
    /* Horizontaler Absprung-Schwung, per Regression aus 21 Slippi-Replays (n = 98-589 Sprünge je Charakter):
     * vx = Stick * a + Bodengeschwindigkeit * b, begrenzt auf den größten beobachteten Wert */
    static const float JUMP_H[NUM_CHARS][3] = {
        { 0.495f, 0.845f, 1.7f },   /* Fox */
        { 0.846f, 0.812f, 2.3f },   /* Falcon */
        { 0.620f, 0.701f, 1.4f },   /* Sheik */
        { 0.696f, 0.611f, 1.2f },   /* Marth */
        { 0.535f, 1.198f, 1.7f },   /* Falco */
    };
    p.jump_h_stick = JUMP_H[c][0];
    p.jump_h_ground = JUMP_H[c][1];
    p.jump_h_max = JUMP_H[c][2];
    p.dash_accel_base = DASH_ACC_FULL[c] / 6.0f;
    p.dash_accel_add = DASH_ACC_FULL[c] * 5.0f / 6.0f;
    p.air_accel_base = r->accel_base;
    p.air_accel_add = r->accel_add;
    p.air_max = r->air_speed;
    p.air_friction = r->air_friction;
    p.gravity = r->gravity;
    p.fall_max = r->fall;
    p.fastfall = r->fastfall;
    p.jumpsquat = r->jumpsquat;
    p.fullhop = r->fullhop_v;
    p.shorthop = r->shorthop_v;
    p.djump = r->djump_v;
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
    const AttackData *a = attack_data(f->character, f->attack);
    int fr = f->state_frame + 1;                   /* Melee-Frame (1-basiert) */
    bool active = false;
    for (int i = 0; i < a->nwin; i++)
        if (fr >= a->win[i][0] && fr <= a->win[i][1]) active = true;
    if (!active) return false;
    center->x = f->pos.x + a->off_x * (float)f->facing * (float)a->kb_dir;
    center->y = f->pos.y + a->off_y;
    *radius = a->radius;
    return true;
}
