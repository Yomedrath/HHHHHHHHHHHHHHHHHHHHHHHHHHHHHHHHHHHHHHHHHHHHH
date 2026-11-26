/* aerial_measure – misst Aerial-Frame-Daten aus Slippi-Replays:
 * Animationslänge, frühester Interrupt, Landing Lag (L-Cancel ja/nein), Autocancel-Fenster.
 *   aerial_measure replay1.slp [...] */
#include <stdio.h>
#include <string.h>
#include "slp.h"

#define NCH 26
#define NA 5
static const char *AN[NA] = { "Nair", "Fair", "Bair", "Uair", "Dair" };

typedef struct {
    int n, max_len, min_interrupt;
    int lag_hist[2][64];          /* [0] ohne L-Cancel, [1] mit L-Cancel */
    unsigned char ac[80];         /* Bit0: Landung ins normale Landing, Bit1: ins LandingAir */
} Stat;

static Stat S[NCH][NA];

int main(int argc, char **argv)
{
    for (int c = 0; c < NCH; c++) for (int a = 0; a < NA; a++) S[c][a].min_interrupt = 999;
    for (int i = 1; i < argc; i++) {
        SlpGame g; char err[128];
        if (!slp_load(argv[i], &g, err, sizeof(err))) continue;
        for (int p = 0; p < SLP_PORTS; p++) {
            if (!g.frames[p]) continue;
            int ch = g.ext_char[p];
            if (ch >= NCH) continue;
            const SlpFrame *F = g.frames[p];
            for (int k = 2; k < g.nframes - 1; k++) {
                int a = F[k].action;
                if (a < 65 || a > 69) continue;
                if (F[k - 1].action == a) continue;                 /* nur Start */
                Stat *s = &S[ch][a - 65];
                s->n++;
                int j = k;
                while (j + 1 < g.nframes && F[j + 1].action == a) j++;
                int len = j - k + 1;
                int nx = j + 1 < g.nframes ? F[j + 1].action : 0;
                bool air_next = j + 1 < g.nframes && F[j + 1].airborne;
                if (air_next && (nx >= 29 && nx <= 34)) { if (len > s->max_len) s->max_len = len; }
                else if (air_next && nx != 0 && nx != 38 && !(nx >= 75 && nx <= 91) && len < s->min_interrupt)
                    s->min_interrupt = len + 1;                    /* erster Frame der neuen Aktion */
                if (!F[j + 1].airborne && len < 80) {
                    if (nx == 42) s->ac[len] |= 1;
                    else if (nx >= 70 && nx <= 74) {
                        s->ac[len] |= 2;
                        int l = j + 1, e = l;
                        while (e + 1 < g.nframes && F[e + 1].action == nx) e++;
                        int dur = e - l + 1;
                        bool interrupted = F[e + 1].airborne;          /* Edgecancel -> nicht zählen */
                        if (!interrupted && dur < 64) s->lag_hist[F[l].lcancel == 1 ? 1 : 0][dur]++;
                    }
                }
            }
        }
        slp_free(&g);
    }
    for (int c = 0; c < NCH; c++) {
        bool any = false;
        for (int a = 0; a < NA; a++) any |= S[c][a].n > 0;
        if (!any) continue;
        printf("== %s ==\n", slp_char_name(c));
        for (int a = 0; a < NA; a++) {
            Stat *s = &S[c][a];
            if (!s->n) continue;
            int mode[2] = { 0, 0 };
            for (int t = 0; t < 2; t++) {
                int best = 0;
                for (int d = 0; d < 64; d++) if (s->lag_hist[t][d] > best) { best = s->lag_hist[t][d]; mode[t] = d; }
            }
            printf("  %-5s n=%3d  Länge %2d  Interrupt ab %3d  Lag %2d / L-Cancel %2d  Autocancel bei Frames:",
                   AN[a], s->n, s->max_len, s->min_interrupt == 999 ? -1 : s->min_interrupt, mode[0], mode[1]);
            for (int f = 1; f < 80; f++) if (s->ac[f]) printf(" %d%s", f, s->ac[f] == 1 ? "" : (s->ac[f] == 2 ? "x" : "?"));
            printf("\n");
        }
    }
    return 0;
}
