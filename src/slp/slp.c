#include "slp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Big-Endian-Leser mit Grenzprüfung ---------- */

typedef struct { const uint8_t *p; size_t n; } Buf;

static uint32_t rd_u32(Buf b, size_t o) { return o + 4 > b.n ? 0 : ((uint32_t)b.p[o] << 24) | ((uint32_t)b.p[o + 1] << 16) | ((uint32_t)b.p[o + 2] << 8) | b.p[o + 3]; }
static uint16_t rd_u16(Buf b, size_t o) { return o + 2 > b.n ? 0 : (uint16_t)((b.p[o] << 8) | b.p[o + 1]); }
static uint8_t rd_u8(Buf b, size_t o) { return o >= b.n ? 0 : b.p[o]; }
static int32_t rd_i32(Buf b, size_t o) { return (int32_t)rd_u32(b, o); }
static float rd_f32(Buf b, size_t o) { uint32_t u = rd_u32(b, o); float f; memcpy(&f, &u, 4); return f; }

static void set_err(char *err, size_t len, const char *msg) { if (err && len) snprintf(err, len, "%s", msg); }

/* "raw"-Array im UBJSON finden: ...U\x03raw[$U#l<int32 len><bytes> */
static bool find_raw(const uint8_t *d, size_t n, const uint8_t **raw, size_t *rawlen)
{
    static const uint8_t key[] = { 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l' };
    for (size_t i = 0; i + sizeof(key) + 4 <= n && i < 64; i++) {
        if (memcmp(d + i, key, sizeof(key)) == 0) {
            size_t o = i + sizeof(key);
            uint32_t len = ((uint32_t)d[o] << 24) | ((uint32_t)d[o + 1] << 16) | ((uint32_t)d[o + 2] << 8) | d[o + 3];
            o += 4;
            /* Live-geschriebene Dateien haben Länge 0 -> bis Dateiende lesen */
            if (len == 0 || o + len > n) len = (uint32_t)(n - o);
            *raw = d + o;
            *rawlen = len;
            return true;
        }
    }
    return false;
}

bool slp_load(const char *path, SlpGame *g, char *err, size_t errlen)
{
    memset(g, 0, sizeof(*g));
    FILE *f = fopen(path, "rb");
    if (!f) { set_err(err, errlen, "Datei nicht lesbar"); return false; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0 || fsz > 512L * 1024 * 1024) { fclose(f); set_err(err, errlen, "ungültige Dateigröße"); return false; }
    uint8_t *data = malloc((size_t)fsz);
    if (!data || fread(data, 1, (size_t)fsz, f) != (size_t)fsz) { fclose(f); free(data); set_err(err, errlen, "Lesefehler"); return false; }
    fclose(f);

    const uint8_t *raw; size_t rawlen;
    if (!find_raw(data, (size_t)fsz, &raw, &rawlen) || rawlen < 2 || raw[0] != 0x35) {
        free(data); set_err(err, errlen, "kein Slippi-Replay (raw/Event Payloads fehlt)"); return false;
    }

    /* Event Payloads: Größe jedes Kommandos */
    uint16_t sizes[256] = { 0 };
    uint8_t psz = raw[1];
    for (size_t k = 2; k + 2 < (size_t)psz + 1 && k + 2 < rawlen; k += 3)
        sizes[raw[k]] = (uint16_t)((raw[k + 1] << 8) | raw[k + 2]);
    size_t start = 1 + (size_t)psz;

    /* Durchgang 1: höchsten Frame finden */
    int max_frame = SLP_FIRST_FRAME;
    for (size_t pos = start; pos < rawlen;) {
        uint8_t cmd = raw[pos];
        if (cmd == 0x35 || sizes[cmd] == 0) break;
        Buf ev = { raw + pos, (size_t)sizes[cmd] + 1 };
        if (pos + ev.n > rawlen) break;
        if (cmd == 0x37 || cmd == 0x38) {
            int fr = rd_i32(ev, 1);
            if (fr > max_frame) max_frame = fr;
        }
        pos += ev.n;
    }
    g->nframes = max_frame - SLP_FIRST_FRAME + 1;

    /* Durchgang 2: Daten füllen */
    bool have_start = false;
    for (size_t pos = start; pos < rawlen;) {
        uint8_t cmd = raw[pos];
        if (cmd == 0x35 || sizes[cmd] == 0) break;
        Buf ev = { raw + pos, (size_t)sizes[cmd] + 1 };
        if (pos + ev.n > rawlen) break;
        pos += ev.n;

        if (cmd == 0x36) {                                   /* Game Start */
            memcpy(g->version, ev.p + 1, 4);
            g->stage = rd_u16(ev, 0x13);
            for (int p = 0; p < SLP_PORTS; p++) {
                g->ext_char[p] = rd_u8(ev, 0x65 + 0x24 * (size_t)p);
                g->type[p] = rd_u8(ev, 0x66 + 0x24 * (size_t)p);
                if (g->type[p] != 3) {
                    g->frames[p] = calloc((size_t)g->nframes, sizeof(SlpFrame));
                    if (!g->frames[p]) { free(data); slp_free(g); set_err(err, errlen, "kein Speicher"); return false; }
                }
            }
            have_start = true;
            continue;
        }
        if (!have_start || (cmd != 0x37 && cmd != 0x38)) continue;
        int fr = rd_i32(ev, 1) - SLP_FIRST_FRAME;
        int port = rd_u8(ev, 5);
        if (rd_u8(ev, 6) != 0) continue;                     /* Nana (Ice Climbers) ignorieren */
        if (fr < 0 || fr >= g->nframes || port >= SLP_PORTS || !g->frames[port]) continue;
        SlpFrame *s = &g->frames[port][fr];

        if (cmd == 0x37) {                                   /* Pre-Frame */
            s->has_pre = true;
            s->stick_x = rd_f32(ev, 0x19);
            s->stick_y = rd_f32(ev, 0x1D);
            s->cstick_x = rd_f32(ev, 0x21);
            s->cstick_y = rd_f32(ev, 0x25);
            s->trigger = rd_f32(ev, 0x29);
            s->buttons = rd_u32(ev, 0x2D);
            s->phys_l = rd_f32(ev, 0x33);
            s->phys_r = rd_f32(ev, 0x37);
        } else {                                             /* Post-Frame */
            s->has_post = true;
            s->action = rd_u16(ev, 0x08);
            s->x = rd_f32(ev, 0x0A);
            s->y = rd_f32(ev, 0x0E);
            s->facing = rd_f32(ev, 0x12);
            s->percent = rd_f32(ev, 0x16);
            s->stocks = rd_u8(ev, 0x21);
            s->action_frame = rd_f32(ev, 0x22);
            s->airborne = rd_u8(ev, 0x2F);
            s->last_ground = rd_u16(ev, 0x30);
            s->jumps_left = rd_u8(ev, 0x32);
            s->self_air_x = rd_f32(ev, 0x35);   /* ab Slippi 3.5.0 */
            s->self_y = rd_f32(ev, 0x39);
            s->attack_x = rd_f32(ev, 0x3D);
            s->attack_y = rd_f32(ev, 0x41);
            s->ground_x = rd_f32(ev, 0x45);
        }
    }
    free(data);
    if (!have_start) { slp_free(g); set_err(err, errlen, "Game-Start-Event fehlt"); return false; }
    return true;
}

void slp_free(SlpGame *g)
{
    for (int p = 0; p < SLP_PORTS; p++) { free(g->frames[p]); g->frames[p] = NULL; }
}

const char *slp_char_name(int c)
{
    static const char *names[] = {
        "Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby", "Bowser", "Link", "Luigi",
        "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Jigglypuff", "Samus",
        "Yoshi", "Zelda", "Sheik", "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf",
    };
    return (c >= 0 && c < (int)(sizeof(names) / sizeof(names[0]))) ? names[c] : "?";
}

const char *slp_stage_name(int s)
{
    switch (s) {
    case 2: return "Fountain of Dreams";
    case 3: return "Pokemon Stadium";
    case 8: return "Yoshi's Story";
    case 28: return "Dream Land";
    case 31: return "Battlefield";
    case 32: return "Final Destination";
    default: return "andere Stage";
    }
}
