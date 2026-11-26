#!/usr/bin/env python3
"""Misst Charakter-Attribute direkt aus Slippi-Replays (Positionsänderungen pro Frame).

    pip install py-slippi
    python3 tools/measure_attributes.py data/replays/*.slp

Geschwindigkeiten werden als Positionsdifferenz x[k] - x[k-1] bestimmt, damit auch alte Replays
(vor Slippi 3.5, ohne gespeicherte Geschwindigkeiten) nutzbar sind. Jeder Wert ist der häufigste
gerundete Messwert (Modus) über alle passenden Frames; n = Anzahl der Messungen.
"""
import collections
import sys

from slippi import Game

WAIT, WALK, TURN, DASH, RUN, KNEE, JUMPF, JUMPB, JUMPAF, JUMPAB = 14, (15, 16, 17), 18, 20, 21, 24, 25, 26, 27, 28
FALL = (29, 30, 31)
LANDING = 42
DAMAGE = range(75, 92)


def mode(values, digits=3, min_n=5):
    vals = [round(v, digits) for v in values]
    if len(vals) < min_n:
        return None, len(vals)
    c = collections.Counter(vals)
    v, _ = c.most_common(1)[0]
    return v, len(vals)


def measure(paths):
    samples = collections.defaultdict(lambda: collections.defaultdict(list))
    for path in paths:
        try:
            g = Game(path)
        except Exception as e:  # defekte Datei überspringen
            print(f"# übersprungen: {path}: {e}", file=sys.stderr)
            continue
        frames = g.frames
        for port, pl in enumerate(g.start.players):
            if pl is None:
                continue
            ch = pl.character.name
            S = samples[ch]
            F = []
            for fr in frames:
                p = fr.ports[port]
                if p is None:
                    F.append(None)
                    continue
                post, pre = p.leader.post, p.leader.pre
                F.append((int(post.state), post.position.x, post.position.y, pre.joystick.x, pre.joystick.y,
                          bool(post.airborne), float(post.state_age or 0), float(post.damage or 0)))
            n = len(F)
            for k in range(3, n - 1):
                if None in (F[k], F[k - 1], F[k - 2], F[k - 3]):
                    continue
                a, x, y, sx, sy, air, age, dmg = F[k]
                pa = F[k - 1][0]
                vx, vy = x - F[k - 1][1], y - F[k - 1][2]
                pvx, pvy = F[k - 1][1] - F[k - 2][1], F[k - 1][2] - F[k - 2][2]
                if a in DAMAGE or pa in DAMAGE or dmg != F[k - 1][7]:
                    continue
                # Jumpsquat-Länge, Sprung-Startgeschwindigkeiten
                if a in (JUMPF, JUMPB) and pa == KNEE:
                    j = k - 1
                    while j > 0 and F[j] and F[j][0] == KNEE:
                        j -= 1
                    S["jumpsquat"].append(k - 1 - j)
                    S["jump_v0"].append(vy)
                if a in (JUMPAF, JUMPAB) and pa not in (JUMPAF, JUMPAB):
                    S["djump_v0"].append(vy)
                # Schwerkraft, Fallgeschwindigkeiten (nur normales Fallen ohne Aktion)
                if a in FALL + (JUMPF, JUMPB) and pa == a and age > 2:
                    dv = vy - pvy
                    if -0.5 < dv < -0.01:
                        S["gravity"].append(-dv)
                    if a in FALL and abs(dv) < 1e-4 and vy < -0.5:
                        S["fall_speeds"].append(-vy)
                # Luftbewegung im normalen Fallen
                if a in FALL and pa == a and age > 2:
                    dvx = abs(vx) - abs(pvx)
                    if abs(sx) > 0.98 and (vx * sx) > 0:
                        if abs(dvx) < 1e-4 and abs(vx) > 0.3:
                            S["air_max"].append(abs(vx))
                        elif dvx > 0.001:
                            S["air_accel_full"].append(dvx)
                    if abs(sx) < 0.2 and abs(pvx) > 0.05 and dvx < -0.001:
                        S["air_friction"].append(-dvx)
                # Boden
                if not air and pa == a:
                    if a == RUN and abs(sx) > 0.98 and abs(vx - pvx) < 1e-4:
                        S["run"].append(abs(vx))
                    if a in WALK and abs(sx) > 0.98 and abs(vx - pvx) < 1e-4:
                        S["walk_full"].append(abs(vx))
                    if a == WAIT and abs(sx) < 0.2 and abs(pvx) > 0.01 and abs(vx) < abs(pvx):
                        S["traction"].append(abs(pvx) - abs(vx))
                    if a == DASH and age >= 3 and abs(sx) > 0.98 and abs(vx) > abs(pvx) + 0.001:
                        S["dash_accel_full"].append(abs(vx) - abs(pvx))
                if a == DASH and pa == DASH and age == 2 and F[k - 2][0] != DASH and abs(F[k - 1][1] - F[k - 2][1]) < 0.01 \
                        and abs(sx) > 0.98:
                    S["dash_frame2"].append(abs(vx))
                # Landing: frühester Frame, in dem eine neue Aktion beginnt (= normaler Landing Lag)
                if pa == LANDING and a != LANDING and not air:
                    S["landing_exit"].append(F[k - 1][6] + 1)
    return samples


def summarize(samples):
    out = {}
    for ch, S in samples.items():
        r = {}
        r["jumpsquat"] = mode(S["jumpsquat"], 0)
        v0 = sorted(S["jump_v0"])
        if len(v0) >= 10:
            c = collections.Counter(round(v, 3) for v in v0).most_common(2)
            hi, lo = max(c[0][0], c[1][0]) if len(c) > 1 else c[0][0], min(c[0][0], c[1][0]) if len(c) > 1 else None
            r["fullhop_v0"] = (hi, len(v0))
            r["shorthop_v0"] = (lo, len(v0))
        r["djump_v0"] = mode(S["djump_v0"])
        r["gravity"] = mode(S["gravity"], 4)
        fs = collections.Counter(round(v, 3) for v in S["fall_speeds"]).most_common(3)
        if fs:
            vals = sorted(v for v, cnt in fs if cnt >= 5)
            if vals:
                r["fall_max"] = (vals[0], len(S["fall_speeds"]))
                if len(vals) > 1:
                    r["fastfall"] = (vals[-1], len(S["fall_speeds"]))
        r["air_max"] = mode(S["air_max"])
        r["air_accel_full"] = mode(S["air_accel_full"], 4)
        r["air_friction"] = mode(S["air_friction"], 4)
        r["run"] = mode(S["run"])
        r["walk_full"] = mode(S["walk_full"])
        r["traction"] = mode(S["traction"], 4)
        r["dash_accel_full"] = mode(S["dash_accel_full"], 4)
        r["dash_frame2"] = mode(S["dash_frame2"])
        r["landing_lag"] = mode(S["landing_exit"], 0)
        out[ch] = r
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    res = summarize(measure(sys.argv[1:]))
    keys = ["jumpsquat", "fullhop_v0", "shorthop_v0", "djump_v0", "gravity", "fall_max", "fastfall",
            "air_max", "air_accel_full", "air_friction", "run", "walk_full", "traction",
            "dash_frame2", "dash_accel_full", "landing_lag"]
    for ch in sorted(res):
        print(f"== {ch} ==")
        for k in keys:
            v = res[ch].get(k)
            if v and v[0] is not None:
                print(f"  {k:16s} {v[0]:>8}   (n={v[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
