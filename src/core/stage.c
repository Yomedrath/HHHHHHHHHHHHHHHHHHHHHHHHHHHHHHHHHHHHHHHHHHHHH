/* Die 5 neutralen Melee-Layouts als reine Kollisionsgeometrie.
 * Maße (Kantenposition, Plattformhöhen/-breiten, Blastzones) nach Community-Messungen.
 * Unterseite der Hauptplattform und Spawnpunkte sind vereinfacht/geschätzt.
 * Bewegliche Plattformen (FoD) stehen fest auf ihrer gemessenen Höhe. */
#include "game.h"

#define SOLID(x0, x1, y, b) { SURF_SOLID, (x0), (x1), (y), (b) }
#define PLAT(x0, x1, y)     { SURF_PLATFORM, (x0), (x1), (y), (y) }

static const Stage STAGES[NUM_STAGES] = {
    {   /* Battlefield-Layout */
        "BF-Layout",
        { SOLID(-68.4f, 68.4f, 0, -30), PLAT(-57.6f, -20.0f, 27.2f), PLAT(20.0f, 57.6f, 27.2f),
          PLAT(-18.8f, 18.8f, 54.4f) },
        4, -224.0f, 224.0f, 200.0f, -108.8f,
        { { -38.8f, 30 }, { 38.8f, 30 } },
    },
    {   /* Final-Destination-Layout */
        "FD-Layout",
        { SOLID(-85.56f, 85.56f, 0, -50) },
        1, -246.0f, 246.0f, 188.0f, -140.0f,
        { { -42.0f, 20 }, { 42.0f, 20 } },
    },
    {   /* Yoshi's-Story-Layout (Seitenblastzones asymmetrisch) */
        "YS-Layout",
        { SOLID(-56.0f, 56.0f, 0, -25), PLAT(-59.5f, -28.0f, 23.45f), PLAT(28.0f, 59.5f, 23.45f),
          PLAT(-15.75f, 15.75f, 42.0f) },
        4, -175.7f, 173.6f, 168.0f, -91.0f,
        { { -42.0f, 30 }, { 42.0f, 30 } },
    },
    {   /* Dream-Land-Layout */
        "DL-Layout",
        { SOLID(-77.27f, 77.27f, 0, -35), PLAT(-61.39f, -30.02f, 30.13f), PLAT(30.02f, 61.39f, 30.13f),
          PLAT(-19.02f, 19.02f, 51.42f) },
        4, -245.0f, 245.0f, 220.0f, -123.0f,
        { { -45.0f, 35 }, { 45.0f, 35 } },
    },
    {   /* Fountain-of-Dreams-Layout (Seitenplattformen fest) */
        "FoD-Layout",
        { SOLID(-63.35f, 63.35f, 0, -40), PLAT(-49.5f, -21.0f, 27.12f), PLAT(21.0f, 49.5f, 27.12f),
          PLAT(-14.25f, 14.25f, 43.83f) },
        4, -198.75f, 198.75f, 202.5f, -146.25f,
        { { -35.0f, 30 }, { 35.0f, 30 } },
    },
};

const Stage *stage_get(int index)
{
    if (index < 0 || index >= NUM_STAGES) index = 0;
    return &STAGES[index];
}
