/* Minimaler Parser für Slippi-Replays (.slp) – liest Game Start, Pre- und Post-Frame-Events.
 * Format: UBJSON-Objekt mit "raw" (Event-Stream, Big Endian) und "metadata".
 * Spezifikation: https://github.com/project-slippi/slippi-wiki/blob/master/SPEC.md */
#ifndef PROTO_SLP_H
#define PROTO_SLP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SLP_PORTS 4
#define SLP_FIRST_FRAME (-123)

/* Ausgewählte Melee-Action-States (gemeinsame IDs für alle Charaktere) */
enum {
    AS_WAIT = 0x0E, AS_WALK_SLOW = 0x0F, AS_WALK_MID = 0x10, AS_WALK_FAST = 0x11,
    AS_TURN = 0x12, AS_TURN_RUN = 0x13, AS_DASH = 0x14, AS_RUN = 0x15, AS_RUN_BRAKE = 0x17,
    AS_KNEE_BEND = 0x18, AS_JUMP_F = 0x19, AS_JUMP_B = 0x1A,
    AS_JUMP_AERIAL_F = 0x1B, AS_JUMP_AERIAL_B = 0x1C,
    AS_FALL = 0x1D, AS_FALL_SPECIAL = 0x23,
    AS_SQUAT = 0x27, AS_SQUAT_WAIT = 0x28, AS_LANDING = 0x2A, AS_LANDING_FALL_SPECIAL = 0x2B,
    AS_ESCAPE_AIR = 0xEC, AS_PASS = 0xF4, AS_CLIFF_CATCH = 0xFC, AS_CLIFF_WAIT = 0xFD,
};

/* Melee-Buttons (Processed Buttons) */
enum {
    SLP_BTN_Z = 0x0010, SLP_BTN_R = 0x0020, SLP_BTN_L = 0x0040,
    SLP_BTN_A = 0x0100, SLP_BTN_B = 0x0200, SLP_BTN_X = 0x0400, SLP_BTN_Y = 0x0800,
    SLP_BTN_START = 0x1000,
};

typedef struct {
    bool has_pre, has_post;
    /* Pre-Frame: Eingaben, die in diesem Frame verarbeitet werden */
    float stick_x, stick_y, cstick_x, cstick_y, trigger;
    uint32_t buttons;
    float phys_l, phys_r;
    /* Post-Frame: Zustand nach diesem Frame */
    uint16_t action;
    float x, y, facing, percent, action_frame;
    uint8_t stocks, airborne, jumps_left;
    uint16_t last_ground;
    float self_air_x, self_y, attack_x, attack_y, ground_x;
} SlpFrame;

typedef struct {
    uint8_t version[4];
    int stage;                      /* Melee-Stage-ID */
    int ext_char[SLP_PORTS];        /* externe Charakter-ID */
    int type[SLP_PORTS];            /* 0 Mensch, 1 CPU, 3 leer */
    int nframes;                    /* Index 0 = Frame -123 */
    SlpFrame *frames[SLP_PORTS];    /* NULL für leere Ports */
} SlpGame;

bool slp_load(const char *path, SlpGame *g, char *err, size_t errlen);
void slp_free(SlpGame *g);

const char *slp_char_name(int ext_char);
const char *slp_stage_name(int stage);

#endif
