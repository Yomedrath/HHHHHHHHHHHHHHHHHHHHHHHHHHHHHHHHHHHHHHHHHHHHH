/* Spiellogik: Zustandsautomat, Physik, Kollision, Treffer, KOs. */
#include "game.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define KB_DECAY 0.051f
#define LEDGE_GETUP_FRAMES 24
#define DASH_FRAMES 12
/* Vereinfachte ECB: In der Luft liegt die Kollisions-Unterkante über den Füßen (Melee-Daten zeigen
 * Füße bis knapp 3 Einheiten unter dem Boden vor der Landung). Wird nur für Boden/Plattformen benutzt. */
#ifndef ECB_AIR_OFFSET
#define ECB_AIR_OFFSET 3.0f
#endif

/* ---------- kleine Helfer ---------- */

static float signf(float v) { return v < 0 ? -1.0f : 1.0f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float approach(float v, float target, float step)
{
    if (v < target) return fminf(v + step, target);
    if (v > target) return fmaxf(v - step, target);
    return v;
}

static void set_state(Fighter *f, FState s) { f->state = s; f->state_frame = 0; }

static bool pressed_a(const Fighter *f)      { return f->in.a && !f->prev.a; }
static bool pressed_shield(const Fighter *f)
{
    return (f->in.shield && !f->prev.shield) || (f->in.l && !f->prev.l) || (f->in.r && !f->prev.r);
}
static bool tap_up(const Fighter *f)    { return f->in.stick_y > 0.66f && f->prev.stick_y <= 0.66f; }
static bool down_hard(const Fighter *f) { return f->in.stick_y < -0.66f && f->prev.stick_y >= -0.66f; }
static bool jump_pressed(const Fighter *f) { return (f->in.jump && !f->prev.jump) || tap_up(f); }

/* Smash-Eingabe: Stick in <= 2 Frames von nahe Mitte (oder Gegenrichtung) auf >= 0.8 */
static bool smash_x(const Fighter *f)
{
    float h0 = f->stick_x_hist[0];
    if (fabsf(h0) < 0.8f) return false;
    float d = signf(h0);
    return f->stick_x_hist[1] * d < 0.3f || f->stick_x_hist[2] * d < 0.3f;
}

/* Melee: über der maximalen Gehgeschwindigkeit bremst der Boden doppelt so stark */
static void ground_friction(Fighter *f)
{
    float t = f->p.ground_friction;
    if (fabsf(f->vel.x) > f->p.walk_max) t *= 2.0f;
    f->vel.x = approach(f->vel.x, 0, t);
}

static void air_drift(Fighter *f, float mult)
{
    float sx = f->in.stick_x;
    if (fabsf(sx) > 0.25f) {
        float target = sx * f->p.air_max;
        /* Melee-artig: Beschleunigung = Basis + Zusatz * Stickauslenkung */
        float accel = (f->p.air_accel_base + f->p.air_accel_add * fabsf(sx)) * mult;
        /* über Maximalgeschwindigkeit nur Luftreibung, kein aktives Abbremsen */
        if (fabsf(f->vel.x) > fabsf(target) && signf(f->vel.x) == signf(sx))
            f->vel.x = approach(f->vel.x, target, f->p.air_friction);
        else
            f->vel.x = approach(f->vel.x, target, accel);
    } else {
        f->vel.x = approach(f->vel.x, 0, f->p.air_friction);
    }
}

static void enter_air(Fighter *f, FState s)
{
    f->grounded = false;
    f->surface = -1;
    set_state(f, s);
}

static void start_attack(Fighter *f, AttackId id)
{
    f->attack = id;
    f->attack_hit = false;
    set_state(f, ST_ATTACK);
}

static void start_airdodge(Fighter *f)
{
    /* Wie Melee: alte Bewegung wird komplett ersetzt. In der Deadzone -> Stillstand,
     * sonst feste Kraft in Richtung des Stickwinkels (Betrag der Auslenkung egal). */
    float sx = f->in.stick_x, sy = f->in.stick_y;
    if (fabsf(sx) < AIRDODGE_DEADZONE && fabsf(sy) < AIRDODGE_DEADZONE) {
        f->vel.x = 0;
        f->vel.y = 0;
    } else {
        float ang = atan2f(sy, sx);
        f->vel.x = AIRDODGE_FORCE * cosf(ang);
        f->vel.y = AIRDODGE_FORCE * sinf(ang);
    }
    f->fastfalling = false;
    set_state(f, ST_AIRDODGE);
}

/* Melee-Dash: Startgeschwindigkeit wird zur aktuellen addiert. Im ersten Frame bewegt sich der
 * Kämpfer noch mit der alten Geschwindigkeit (die neue wirkt ab dem nächsten Frame). */
static void start_dash(Fighter *f, int dir)
{
    float old = f->vel.x;
    f->facing = dir;
    f->smash_turn = false;
    f->vel.x = old + (float)dir * f->p.dash_init;
    f->pos.x -= f->vel.x - old;          /* Physik bewegt gleich um vel.x -> effektiv um "old" */
    set_state(f, ST_DASH);
}

static void start_turn(Fighter *f, bool smash)
{
    if (f->state == ST_DASH) {
        /* Zurückdashen: Melee drittelt-viertelt den Schwung (gemessen: v * 0.25 - Traction) */
        f->vel.x *= 0.25f;
        f->vel.x = approach(f->vel.x, 0, f->p.ground_friction);
    }
    f->facing = -f->facing;
    f->smash_turn = smash;
    set_state(f, ST_TURN);
}

static void land(Game *g, Fighter *f, int surf)
{
    FState from = f->state;
    f->grounded = true;
    f->surface = surf;
    f->pos.y = g->stage->surf[surf].y;
    f->vel.y = 0;
    f->kb.y = 0;
    f->kb.x *= 0.5f;
    f->jumps_left = f->p.max_jumps;
    f->fastfalling = false;

    int lag = f->p.land_lag;
    if (from == ST_AIRDODGE) lag = AIRDODGE_LAND_LAG;          /* Wavedash / Waveland */
    else if (from == ST_FREEFALL) lag = 10;
    else if (from == ST_ATTACK && attack_data(f->character, f->attack)->aerial) {
        /* Aerial-Landung: im Autocancel-Fenster normales Landing, sonst Landing Lag
         * (mit L-Cancel halbiert, abgerundet – wie in Melee) */
        const AttackData *a = attack_data(f->character, f->attack);
        int fr = f->state_frame + 1;
        bool autocancel = (a->ac_early > 0 && fr <= a->ac_early) || (a->ac_late > 0 && fr >= a->ac_late);
        f->last_lcancel = false;
        if (!autocancel) {
            f->last_lcancel = f->lcancel_timer > 0;
            lag = f->last_lcancel ? a->landing_lag / 2 : a->landing_lag;
        }
    }
    else if (from == ST_HITSTUN) { lag = 6; f->hitstun = 0; }

    f->attack = ATK_NONE;
    f->landing_lag = lag;
    set_state(f, ST_LANDING);
}

/* Bodenaktionen, die aus fast jedem Bodenzustand gehen. */
static bool try_ground_actions(Game *g, Fighter *f)
{
    if (jump_pressed(f)) {
        set_state(f, ST_JUMPSQUAT);
        return true;
    }
    if (pressed_a(f)) {
        if (fabsf(f->in.stick_x) >= 0.8f) {
            f->facing = (int)signf(f->in.stick_x);
            start_attack(f, ATK_FSTRONG);
        } else {
            start_attack(f, ATK_JAB);
        }
        return true;
    }
    if (down_hard(f) && f->surface >= 0 && g->stage->surf[f->surface].kind == SURF_PLATFORM) {
        f->drop_through = 10;
        f->pos.y -= 0.5f;
        f->jumps_left = f->p.max_jumps - 1;
        enter_air(f, ST_AIR);
        return true;
    }
    return false;
}

/* Welches Aerial? A + Stickrichtung oder C-Stick-Flick (relativ zur Blickrichtung). */
static AttackId aerial_from_dir(const Fighter *f, float x, float y, bool cstick)
{
    float mag = sqrtf(x * x + y * y);
    if (!cstick && mag < 0.3f) return ATK_NAIR;
    if (fabsf(y) >= fabsf(x)) return y > 0 ? ATK_UAIR : ATK_DAIR;
    return x * (float)f->facing > 0 ? ATK_FAIR : ATK_BAIR;
}

static AttackId aerial_input(const Fighter *f)
{
    float cx = f->in.cstick_x, cy = f->in.cstick_y;
    float pcx = f->prev.cstick_x, pcy = f->prev.cstick_y;
    bool cflick = (cx * cx + cy * cy) > 0.36f && (pcx * pcx + pcy * pcy) <= 0.36f;
    if (cflick) return aerial_from_dir(f, cx, cy, true);
    if (pressed_a(f)) return aerial_from_dir(f, f->in.stick_x, f->in.stick_y, false);
    return ATK_NONE;
}

/* Luftaktionen (Doppelsprung, Airdodge, Aerials, Fastfall). */
static bool try_air_actions(Fighter *f)
{
    if (jump_pressed(f) && f->jumps_left > 0) {
        f->vel.y = f->p.djump;
        f->vel.x = f->in.stick_x * f->p.air_max;
        f->jumps_left--;
        f->fastfalling = false;
        set_state(f, ST_AIR);
        return true;
    }
    if (pressed_shield(f)) { start_airdodge(f); return true; }
    AttackId aer = aerial_input(f);
    if (aer != ATK_NONE) { start_attack(f, aer); return true; }
    return false;
}

static void check_fastfall(Fighter *f)
{
    if (!f->fastfalling && f->vel.y <= 0 && down_hard(f)) f->fastfalling = true;
}

/* ---------- Kanten ---------- */

static void ledge_info(const Stage *st, int ledge, float *lx, float *ly, int *out)
{
    const Surface *s = &st->surf[ledge / 2];
    int right = ledge % 2;
    *lx = right ? s->x1 : s->x0;
    *ly = s->y;
    *out = right ? 1 : -1;
}

static void ledge_hang_pos(const Fighter *f, const Stage *st, Vec2 *p)
{
    float lx, ly; int out;
    ledge_info(st, f->ledge, &lx, &ly, &out);
    p->x = lx + (float)out * (f->p.width * 0.5f + 0.5f);
    p->y = ly - f->p.height * 0.85f;
}

static bool ledge_taken(const Game *g, const Fighter *self, int ledge)
{
    for (int i = 0; i < MAX_FIGHTERS; i++) {
        const Fighter *o = &g->f[i];
        if (o != self && o->ledge == ledge && (o->state == ST_LEDGE || o->state == ST_LEDGE_GETUP))
            return true;
    }
    return false;
}

static bool try_ledge_grab(Game *g, Fighter *f)
{
    if (f->ledge_cooldown > 0 || f->vel.y + f->kb.y >= 0) return false;
    const Stage *st = g->stage;
    for (int i = 0; i < st->nsurf; i++) {
        const Surface *s = &st->surf[i];
        if (s->kind != SURF_SOLID) continue;
        for (int side = 0; side < 2; side++) {
            int id = i * 2 + side;
            float lx, ly; int out;
            ledge_info(st, id, &lx, &ly, &out);
            float dx = (f->pos.x - lx) * (float)out;   /* > 0 = außerhalb der Stage */
            if (dx > -3.0f && dx < 12.0f && f->pos.y < ly && f->pos.y > ly - f->p.height - 4.0f
                && !ledge_taken(g, f, id)) {
                f->ledge = id;
                f->facing = -out;
                f->vel.x = f->vel.y = 0;
                f->kb.x = f->kb.y = 0;
                f->jumps_left = f->p.max_jumps - 1;
                f->fastfalling = false;
                f->invuln = 30;
                f->attack = ATK_NONE;
                enter_air(f, ST_LEDGE);
                ledge_hang_pos(f, st, &f->pos);
                return true;
            }
        }
    }
    return false;
}

static void leave_ledge(Fighter *f)
{
    f->ledge = -1;
    f->ledge_cooldown = 30;
}

/* ---------- Zustandslogik ---------- */

static void update_state(Game *g, Fighter *f)
{
    float sx = f->in.stick_x, sy = f->in.stick_y;
    const Stage *st = g->stage;

    switch (f->state) {
    case ST_IDLE:
        if (try_ground_actions(g, f)) break;
        ground_friction(f);
        if (smash_x(f)) {
            if ((int)signf(sx) != f->facing) start_turn(f, true);   /* Melee: erst 1 Frame Turn */
            else start_dash(f, (int)signf(sx));
        } else if (fabsf(sx) > 0.25f) {
            if ((int)signf(sx) != f->facing) start_turn(f, false);
            else set_state(f, ST_WALK);
        } else if (sy < -0.6f) set_state(f, ST_CROUCH);
        break;

    case ST_WALK:
        if (try_ground_actions(g, f)) break;
        if (smash_x(f)) {
            if ((int)signf(sx) != f->facing) start_turn(f, true);
            else start_dash(f, (int)signf(sx));
        }
        else if (fabsf(sx) < 0.25f) { ground_friction(f); set_state(f, ST_IDLE); }
        else if ((int)signf(sx) != f->facing) start_turn(f, false);
        else f->vel.x = approach(f->vel.x, sx * f->p.walk_max, f->p.walk_accel);
        break;

    case ST_DASH:
        if (try_ground_actions(g, f)) break;
        if (smash_x(f) && (int)signf(sx) != f->facing) { start_turn(f, true); break; } /* Dashdance */
        {
            /* Beschleunigen bis Run-Speed * |Stick|, sonst mit Traction bremsen */
            float along = sx * (float)f->facing;
            float speed = f->vel.x * (float)f->facing;
            float cap = f->p.run_max * fmaxf(along, 0.0f);
            if (along > 0.25f && speed < cap)
                speed = fminf(speed + f->p.dash_accel_base + f->p.dash_accel_add * along, cap);
            else
                speed = approach(speed, along > 0.25f ? cap : 0.0f, f->p.ground_friction);
            f->vel.x = speed * (float)f->facing;
        }
        if (f->state_frame >= DASH_FRAMES) {
            if (sx * (float)f->facing > 0.6f) set_state(f, ST_RUN);
            else set_state(f, ST_IDLE);
        }
        break;

    case ST_RUN:
        if (try_ground_actions(g, f)) break;
        if (sx * (float)f->facing < -0.6f) start_turn(f, false);
        else if (fabsf(sx) < 0.25f) set_state(f, ST_IDLE);
        else f->vel.x = approach(f->vel.x, (float)f->facing * f->p.run_max, 0.15f);
        break;

    case ST_TURN:
        if (try_ground_actions(g, f)) break;
        ground_friction(f);
        if (smash_x(f) || (f->smash_turn && sx * (float)f->facing > 0.8f)) start_dash(f, (int)signf(sx));
        else if (f->state_frame >= 3) set_state(f, fabsf(sx) > 0.25f ? ST_WALK : ST_IDLE);
        break;

    case ST_CROUCH:
        if (try_ground_actions(g, f)) break;
        ground_friction(f);
        if (sy > -0.5f) set_state(f, ST_IDLE);
        break;

    case ST_JUMPSQUAT:
        ground_friction(f);
        if (f->state_frame >= f->p.jumpsquat) {
            /* Full Hop, wenn im letzten Jumpsquat-Frame noch eine Sprungeingabe gehalten wurde
             * (Slippi-Daten: Loslassen genau im Absprung-Frame ergibt trotzdem Full Hop) */
            bool full = f->prev.jump || f->prev.stick_y > 0.66f;
            f->vel.y = full ? f->p.fullhop : f->p.shorthop;
            f->vel.x = clampf(f->vel.x * f->p.jump_h_ground + sx * f->p.jump_h_stick, -f->p.jump_h_max, f->p.jump_h_max);
            f->jumps_left = f->p.max_jumps - 1;
            enter_air(f, ST_AIR);
            /* L/R im Absprung-Frame: Airdodge ersetzt den Sprung sofort (Wavedash) */
            if (pressed_shield(f)) start_airdodge(f);
        }
        break;

    case ST_LANDING:
        /* Nach dem Landing Lag ist der Kämpfer im selben Frame handlungsfähig (Melee: Landing ist
         * ab dem Lag-Ende interruptible, z. B. Sprung direkt aus der Landeanimation) */
        if (f->state_frame >= f->landing_lag) {
            set_state(f, ST_IDLE);
            if (try_ground_actions(g, f)) break;
        }
        ground_friction(f);
        break;

    case ST_ATTACK: {
        const AttackData *a = attack_data(f->character, f->attack);
        if (a->aerial) {
            int fr = f->state_frame + 1;
            /* ab IASA: Aerial lässt sich durch andere Luftaktionen abbrechen (Aerial Interrupt) */
            if (a->iasa > 0 && fr >= a->iasa) {
                AttackId keep = f->attack;
                f->attack = ATK_NONE;
                if (try_air_actions(f)) break;
                f->attack = keep;
            }
            air_drift(f, 1.0f);
            check_fastfall(f);
            if (fr >= a->total) { f->attack = ATK_NONE; set_state(f, ST_AIR); }
        } else {
            ground_friction(f);
            if (f->state_frame >= a->total) { f->attack = ATK_NONE; set_state(f, ST_IDLE); }
        }
        break;
    }

    case ST_AIR:
        if (try_air_actions(f)) break;
        air_drift(f, 1.0f);
        check_fastfall(f);
        try_ledge_grab(g, f);
        break;

    case ST_FREEFALL:
        air_drift(f, 0.6f);
        try_ledge_grab(g, f);
        break;

    case ST_AIRDODGE:
        /* kein Ledge-Grab während der Animation; danach hilflos */
        if (f->state_frame >= AIRDODGE_LENGTH) set_state(f, ST_FREEFALL);
        break;

    case ST_HITSTUN:
        if (--f->hitstun <= 0) set_state(f, ST_AIR);
        break;

    case ST_LEDGE: {
        float lx, ly; int out;
        ledge_info(st, f->ledge, &lx, &ly, &out);
        int toward = -out;
        if (f->state_frame >= 8) {
            if (f->in.jump && !f->prev.jump) {          /* nur Sprungtaste, Stick hoch = Getup */
                leave_ledge(f);
                f->vel.y = f->p.fullhop;
                f->vel.x = (float)toward * 0.6f;
                f->jumps_left = f->p.max_jumps - 1;
                set_state(f, ST_AIR);
            } else if (sy > 0.66f || sx * (float)toward > 0.66f) {
                f->invuln = LEDGE_GETUP_FRAMES + 4;
                set_state(f, ST_LEDGE_GETUP);
            } else if (sy < -0.66f || sx * (float)out > 0.66f || f->state_frame > 300) {
                leave_ledge(f);
                f->pos.x += (float)out * 2.0f;
                set_state(f, ST_AIR);
            }
        }
        break;
    }

    case ST_LEDGE_GETUP: {
        float lx, ly; int out;
        ledge_info(st, f->ledge, &lx, &ly, &out);
        Vec2 hang; ledge_hang_pos(f, st, &hang);
        float tx = lx - (float)out * (f->p.width * 0.5f + 3.0f);
        float t = clampf((float)f->state_frame / (float)LEDGE_GETUP_FRAMES, 0, 1);
        f->pos.x = hang.x + (tx - hang.x) * t;
        f->pos.y = hang.y + (ly - hang.y) * t;
        if (f->state_frame >= LEDGE_GETUP_FRAMES) {
            int surf = f->ledge / 2;
            f->ledge = -1;
            f->ledge_cooldown = 20;
            f->grounded = true;
            f->surface = surf;
            f->pos.x = tx;
            f->pos.y = ly;
            f->jumps_left = f->p.max_jumps;
            f->vel.x = f->vel.y = 0;
            set_state(f, ST_IDLE);
        }
        break;
    }

    case ST_DEAD:
    default:
        break;
    }
}

/* ---------- Physik & Kollision ---------- */

static void decay_knockback(Fighter *f)
{
    float len = sqrtf(f->kb.x * f->kb.x + f->kb.y * f->kb.y);
    if (len <= KB_DECAY) { f->kb.x = f->kb.y = 0; return; }
    float s = (len - KB_DECAY) / len;
    f->kb.x *= s;
    f->kb.y *= s;
}

static void fall_off(Fighter *f)
{
    f->jumps_left = f->p.max_jumps - 1;
    f->attack = ATK_NONE;
    enter_air(f, ST_AIR);
}

static void physics(Game *g, Fighter *f)
{
    const Stage *st = g->stage;
    if (f->state == ST_LEDGE || f->state == ST_LEDGE_GETUP || f->state == ST_DEAD) return;

    if (f->grounded) {
        f->vel.y = 0;
        f->pos.x += f->vel.x + f->kb.x;
        decay_knockback(f);
        const Surface *s = &st->surf[f->surface];
        if (f->pos.x < s->x0 || f->pos.x > s->x1) fall_off(f);
        return;
    }

    if (f->fastfalling && f->state != ST_AIRDODGE) f->vel.y = -f->p.fastfall;

    /* Airdodge: Geschwindigkeit dämpfen statt Schwerkraft (state_frame 0 = Frame 1) */
    bool dodge_decay = (f->state == ST_AIRDODGE && f->state_frame < AIRDODGE_DECAY_END);
    if (dodge_decay) {
        f->vel.x *= AIRDODGE_DECAY;
        f->vel.y *= AIRDODGE_DECAY;
    }

    /* erst bewegen, dann Schwerkraft (wie in Melee: Höhe ≈ v²/2g + v/2) */
    Vec2 old = f->pos;
    f->pos.x += f->vel.x + f->kb.x;
    f->pos.y += f->vel.y + f->kb.y;
    float vy_total = f->vel.y + f->kb.y;
    decay_knockback(f);

    if (!dodge_decay) {
        f->vel.y -= f->p.gravity;
        if (f->fastfalling) f->vel.y = -f->p.fastfall;
        else if (f->vel.y < -f->p.fall_max) f->vel.y = -f->p.fall_max;
        if (f->state == ST_AIRDODGE) f->vel.x = approach(f->vel.x, 0, f->p.air_friction);
    }

    /* Landung auf Oberkanten (geprüft an der ECB-Unterkante) */
    float ecb = ECB_AIR_OFFSET;
    if (vy_total <= 0) {
        for (int i = 0; i < st->nsurf; i++) {
            const Surface *s = &st->surf[i];
            if (s->kind == SURF_PLATFORM && f->drop_through > 0) continue;
            if (old.y + ecb >= s->y - 0.01f && f->pos.y + ecb <= s->y && f->pos.x >= s->x0 && f->pos.x <= s->x1) {
                land(g, f, i);
                return;
            }
        }
    }

    /* Seiten und Unterseite fester Blöcke */
    float hw = f->p.width * 0.5f, h = f->p.height;
    for (int i = 0; i < st->nsurf; i++) {
        const Surface *s = &st->surf[i];
        if (s->kind != SURF_SOLID) continue;
        float L = f->pos.x - hw, R = f->pos.x + hw, B = f->pos.y + ecb, T = f->pos.y + h;
        if (R > s->x0 && L < s->x1 && T > s->bottom && B < s->y) {
            if (old.y + h <= s->bottom + 0.01f) {
                f->pos.y = s->bottom - h;
                if (f->vel.y > 0) f->vel.y = 0;
                if (f->kb.y > 0) f->kb.y = 0;
            } else {
                f->pos.x = (f->pos.x < (s->x0 + s->x1) * 0.5f) ? s->x0 - hw : s->x1 + hw;
                f->vel.x = 0;
                f->kb.x = 0;
            }
        }
    }
}

/* ---------- Treffer ---------- */

static bool circle_hits_body(Vec2 c, float r, const Fighter *t)
{
    float hw = t->p.width * 0.5f;
    float px = clampf(c.x, t->pos.x - hw, t->pos.x + hw);
    float py = clampf(c.y, t->pos.y, t->pos.y + t->p.height);
    float dx = c.x - px, dy = c.y - py;
    return dx * dx + dy * dy <= r * r;
}

static void apply_hit(Fighter *atk, Fighter *t)
{
    const AttackData *a = attack_data(atk->character, atk->attack);
    t->percent = fminf(t->percent + a->damage, 999.0f);
    /* Gewichtsfaktor wie in Melee: 200 / (Gewicht + 100) */
    float speed = (a->base_kb + t->percent * a->kb_growth) * (200.0f / (t->p.weight + 100.0f));
    float ang = a->angle_deg * PI_F / 180.0f;
    t->kb.x = cosf(ang) * (float)atk->facing * (float)a->kb_dir * speed;
    t->kb.y = sinf(ang) * speed;
    t->vel.x = t->vel.y = 0;
    t->pos.y += 0.1f;
    t->hitstun = (int)(speed * 10.0f) + 8;
    t->fastfalling = false;
    t->ledge = -1;
    t->attack = ATK_NONE;
    t->facing = -atk->facing * a->kb_dir;
    enter_air(t, ST_HITSTUN);
    atk->attack_hit = true;
}

/* ---------- KO / Respawn ---------- */

static void respawn(Game *g, int i)
{
    Fighter *f = &g->f[i];
    f->pos.x = g->stage->spawn[i].x;
    f->pos.y = g->stage->spawn[i].y + 50.0f;
    f->vel.x = f->vel.y = 0;
    f->kb.x = f->kb.y = 0;
    f->percent = 0;
    f->invuln = 120;
    f->ledge = -1;
    f->attack = ATK_NONE;
    f->fastfalling = false;
    f->jumps_left = f->p.max_jumps;
    enter_air(f, ST_AIR);
}

static void check_blastzones(Game *g)
{
    const Stage *st = g->stage;
    for (int i = 0; i < MAX_FIGHTERS; i++) {
        Fighter *f = &g->f[i];
        if (f->state == ST_DEAD) continue;
        if (f->pos.x < st->blast_left || f->pos.x > st->blast_right ||
            f->pos.y < st->blast_bottom || f->pos.y > st->blast_top) {
            f->stocks--;
            if (f->stocks <= 0) {
                f->stocks = 0;
                f->vel.x = f->vel.y = f->kb.x = f->kb.y = 0;
                f->grounded = false;
                set_state(f, ST_DEAD);
                g->over = true;
                g->winner = 1 - i;
            } else {
                respawn(g, i);
            }
        }
    }
}

/* ---------- öffentliche API ---------- */

bool fighter_intangible(const Fighter *f)
{
    if (f->invuln > 0) return true;
    return f->state == ST_AIRDODGE && f->state_frame + 1 >= AIRDODGE_INTANG_FROM &&
           f->state_frame + 1 <= AIRDODGE_INTANG_TO;
}

void game_init(Game *g, int stage_index, const CharId chars[MAX_FIGHTERS])
{
    memset(g, 0, sizeof(*g));
    g->stage_index = stage_index;
    g->stage = stage_get(stage_index);
    g->winner = -1;
    for (int i = 0; i < MAX_FIGHTERS; i++) {
        Fighter *f = &g->f[i];
        f->character = chars[i];
        f->p = fighter_params(chars[i]);
        f->pos = g->stage->spawn[i];
        f->facing = (i == 0) ? 1 : -1;
        f->stocks = START_STOCKS;
        f->surface = -1;
        f->ledge = -1;
        f->jumps_left = f->p.max_jumps;
        set_state(f, ST_AIR);
    }
}

void game_step(Game *g, const Input inputs[MAX_FIGHTERS])
{
    if (g->over) return;

    for (int i = 0; i < MAX_FIGHTERS; i++) {
        Fighter *f = &g->f[i];
        f->prev = f->in;
        f->in = inputs[i];
        f->in.stick_x = clampf(f->in.stick_x, -1, 1);
        f->in.stick_y = clampf(f->in.stick_y, -1, 1);
        f->stick_x_hist[2] = f->stick_x_hist[1];
        f->stick_x_hist[1] = f->stick_x_hist[0];
        f->stick_x_hist[0] = f->in.stick_x;

        if (f->state == ST_DEAD) continue;
        /* L-Cancel: L, R oder Z in der Luft startet ein 7-Frame-Fenster */
        if (f->lcancel_timer > 0) f->lcancel_timer--;
        if (!f->grounded && (pressed_shield(f) || (f->in.z && !f->prev.z))) f->lcancel_timer = LCANCEL_WINDOW;
        f->state_frame++;
        update_state(g, f);
        physics(g, f);
        if (f->invuln > 0) f->invuln--;
        if (f->ledge_cooldown > 0) f->ledge_cooldown--;
        if (f->drop_through > 0) f->drop_through--;
    }

    for (int i = 0; i < MAX_FIGHTERS; i++) {
        Fighter *a = &g->f[i], *t = &g->f[1 - i];
        Vec2 c; float r;
        if (a->attack_hit || !fighter_hitbox(a, &c, &r)) continue;
        if (t->state == ST_DEAD || fighter_intangible(t)) continue;
        if (circle_hits_body(c, r, t)) apply_hit(a, t);
    }

    check_blastzones(g);
    g->frame++;
}
