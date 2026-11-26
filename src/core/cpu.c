/* Sehr einfacher CPU-Gegner: läuft hin, springt, greift an, recovered zur Stage. */
#include "game.h"
#include <math.h>
#include <string.h>

static uint32_t rng_next(CpuState *c)
{
    c->rng ^= c->rng << 13;
    c->rng ^= c->rng >> 17;
    c->rng ^= c->rng << 5;
    return c->rng;
}

void cpu_init(CpuState *c, uint32_t seed)
{
    memset(c, 0, sizeof(*c));
    c->rng = seed ? seed : 0x12345678u;
}

static const Surface *main_stage(const Stage *st)
{
    for (int i = 0; i < st->nsurf; i++)
        if (st->surf[i].kind == SURF_SOLID) return &st->surf[i];
    return &st->surf[0];
}

Input cpu_think(CpuState *c, const Game *g, int self)
{
    Input in;
    memset(&in, 0, sizeof(in));
    const Fighter *me = &g->f[self];
    const Fighter *op = &g->f[1 - self];
    const Surface *ms = main_stage(g->stage);
    c->press_phase ^= 1;                       /* Tasten nur jeden 2. Frame -> echte Flanken */
    bool can_press = c->press_phase != 0;

    if (me->state == ST_DEAD) return in;

    /* gehaltene Sprungtaste für Full Hop */
    if (c->timer > 0) { c->timer--; in.jump = true; }

    if (me->state == ST_LEDGE) {
        if (me->state_frame > 10 + (int)(rng_next(c) % 20)) in.stick_y = 1.0f;
        return in;
    }

    bool offstage = me->pos.x < ms->x0 || me->pos.x > ms->x1 || me->pos.y < ms->y - 1.0f;
    if (!me->grounded && offstage) {
        float dir = me->pos.x < 0 ? 1.0f : -1.0f;
        in.stick_x = dir;
        bool low = me->pos.y < ms->y + 5.0f;
        if (low && me->vel.y < 0 && me->jumps_left > 0 && can_press) in.jump = true;
        else if (me->pos.y < ms->y - 25.0f && me->jumps_left == 0 && me->state == ST_AIR && can_press) {
            in.shield = true;                  /* Airdodge Richtung Stage als letzte Rettung */
            in.stick_y = 0.6f;
        }
        return in;
    }

    float dx = op->pos.x - me->pos.x;
    float dy = op->pos.y - me->pos.y;
    float adx = fabsf(dx);
    float dir = dx < 0 ? -1.0f : 1.0f;

    /* nicht von der Kante laufen */
    bool near_edge = (dir < 0 && me->pos.x < ms->x0 + 12.0f) || (dir > 0 && me->pos.x > ms->x1 - 12.0f);

    if (op->state != ST_DEAD && adx > 14.0f && !near_edge) in.stick_x = dir;
    else if (adx > 2.0f) in.stick_x = dir * 0.3f;       /* langsam ausrichten */

    if (op->state == ST_DEAD) return in;

    uint32_t r = rng_next(c) % 100;
    if (me->grounded) {
        if (dy > 18.0f && adx < 30.0f && r < 30 && can_press && me->state != ST_JUMPSQUAT) {
            in.jump = true;
            c->timer = 6;                                  /* Full Hop */
        } else if (dy < -15.0f && adx < 40.0f && me->surface >= 0 &&
                   g->stage->surf[me->surface].kind == SURF_PLATFORM && r < 20) {
            in.stick_y = (c->press_phase ? -1.0f : 0.0f);  /* durch Plattform fallen */
        }
        else if (adx < 14.0f && fabsf(dy) < 10.0f && can_press && r < 40) {
            in.a = true;
            if (r < 12) in.stick_x = dir;                 /* Strong-Angriff */
        }
    } else {
        if (adx < 12.0f && fabsf(dy) < 12.0f && can_press && r < 50) in.a = true;
        else if (dy > 15.0f && me->vel.y < 0.5f && me->jumps_left > 0 && can_press && r < 15) in.jump = true; /* Doppelsprung */
        if (me->vel.y < -0.5f && op->pos.y < me->pos.y - 10.0f && r < 10) in.stick_y = -1.0f; /* Fastfall */
    }
    return in;
}
