/* Input-Replays: Die Simulation ist deterministisch, also reicht es, die Eingaben zu speichern.
 * Format: "P1V1RPL1" | u32 Stage | u32 Char0 | u32 Char1 | u32 Frames | Frames * 2 * 6 Byte */
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTES_PER_PLAYER 6
static const char MAGIC[8] = { 'P', '1', 'V', '1', 'R', 'P', 'L', '1' };

static int8_t q8(float v)
{
    float r = roundf(v * 127.0f);
    return (int8_t)(r > 127 ? 127 : (r < -127 ? -127 : r));
}

Input input_quantize(Input in)
{
    in.stick_x = q8(in.stick_x) / 127.0f;
    in.stick_y = q8(in.stick_y) / 127.0f;
    in.cstick_x = q8(in.cstick_x) / 127.0f;
    in.cstick_y = q8(in.cstick_y) / 127.0f;
    return in;
}

static void pack(const Input *in, uint8_t *o)
{
    o[0] = (uint8_t)q8(in->stick_x);
    o[1] = (uint8_t)q8(in->stick_y);
    o[2] = (uint8_t)q8(in->cstick_x);
    o[3] = (uint8_t)q8(in->cstick_y);
    o[4] = (uint8_t)((in->a ? 1 : 0) | (in->b ? 2 : 0) | (in->jump ? 4 : 0) | (in->shield ? 8 : 0) |
                     (in->z ? 16 : 0) | (in->start ? 32 : 0) | (in->l ? 64 : 0) | (in->r ? 128 : 0));
    o[5] = 0;
}

static void unpack(const uint8_t *o, Input *in)
{
    in->stick_x = (int8_t)o[0] / 127.0f;
    in->stick_y = (int8_t)o[1] / 127.0f;
    in->cstick_x = (int8_t)o[2] / 127.0f;
    in->cstick_y = (int8_t)o[3] / 127.0f;
    in->a = o[4] & 1; in->b = o[4] & 2; in->jump = o[4] & 4;
    in->shield = o[4] & 8; in->z = o[4] & 16; in->start = o[4] & 32;
    in->l = o[4] & 64; in->r = o[4] & 128;
}

void replay_init(Replay *r, int stage, const CharId chars[MAX_FIGHTERS])
{
    memset(r, 0, sizeof(*r));
    r->stage = stage;
    for (int i = 0; i < MAX_FIGHTERS; i++) r->chars[i] = chars[i];
}

void replay_push(Replay *r, const Input in[MAX_FIGHTERS])
{
    if (r->nframes == r->cap) {
        int cap = r->cap ? r->cap * 2 : 60 * 60;
        uint8_t *d = realloc(r->data, (size_t)cap * MAX_FIGHTERS * BYTES_PER_PLAYER);
        if (!d) return;
        r->data = d;
        r->cap = cap;
    }
    for (int i = 0; i < MAX_FIGHTERS; i++)
        pack(&in[i], r->data + ((size_t)r->nframes * MAX_FIGHTERS + i) * BYTES_PER_PLAYER);
    r->nframes++;
}

void replay_get(const Replay *r, int frame, Input out[MAX_FIGHTERS])
{
    memset(out, 0, sizeof(Input) * MAX_FIGHTERS);
    if (frame < 0 || frame >= r->nframes) return;
    for (int i = 0; i < MAX_FIGHTERS; i++)
        unpack(r->data + ((size_t)frame * MAX_FIGHTERS + i) * BYTES_PER_PLAYER, &out[i]);
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static bool get32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

bool replay_save(const Replay *r, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(MAGIC, 1, 8, f);
    put32(f, (uint32_t)r->stage);
    for (int i = 0; i < MAX_FIGHTERS; i++) put32(f, (uint32_t)r->chars[i]);
    put32(f, (uint32_t)r->nframes);
    size_t n = (size_t)r->nframes * MAX_FIGHTERS * BYTES_PER_PLAYER;
    bool ok = n == 0 || fwrite(r->data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

bool replay_load(Replay *r, const char *path)
{
    memset(r, 0, sizeof(*r));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[8];
    uint32_t st, c0, c1, n;
    bool ok = fread(magic, 1, 8, f) == 8 && memcmp(magic, MAGIC, 8) == 0 &&
              get32(f, &st) && get32(f, &c0) && get32(f, &c1) && get32(f, &n) &&
              st < NUM_STAGES && c0 < NUM_CHARS && c1 < NUM_CHARS && n < 60u * 60u * 60u;
    if (ok) {
        r->stage = (int)st;
        r->chars[0] = (CharId)c0;
        r->chars[1] = (CharId)c1;
        size_t bytes = (size_t)n * MAX_FIGHTERS * BYTES_PER_PLAYER;
        r->data = malloc(bytes ? bytes : 1);
        ok = r->data && fread(r->data, 1, bytes, f) == bytes;
        r->nframes = r->cap = ok ? (int)n : 0;
    }
    fclose(f);
    if (!ok) replay_free(r);
    return ok;
}

void replay_free(Replay *r)
{
    free(r->data);
    r->data = NULL;
    r->nframes = r->cap = 0;
}
