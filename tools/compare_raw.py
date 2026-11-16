#!/usr/bin/env python3
"""Compare two outputs of tests/render.c (raw float32 stereo, 24 models x 360 blocks of 128 frames), model by model.

    compare_raw.py <a.raw> <b.raw> [<models from params.h>]

Prints, per model: RMS level of each file and the RMS of their difference relative to the signal (dB). Two builds of the
same source with different compilers or CPUs are not bit-identical (float contraction, vectorised sums), and the models
with feedback or noise drift apart over a phrase, so this is a sanity check on level and silence, not a null test.
Exit 1 if a model is silent in one file and not the other, a file holds NaN/Inf, or the sizes differ."""
import array
import math
import re
import sys

BLOCKS_PER_MODEL = 6 * 60
FRAMES = BLOCKS_PER_MODEL * 128


def load(path):
    a = array.array("f")
    data = open(path, "rb").read()
    a.frombytes(data[:len(data) // 4 * 4])
    return a


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def db(x):
    return "%7.1f" % (20 * math.log10(x)) if x > 0 else "   -inf"


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    names = []
    if len(sys.argv) > 3:
        m = re.search(r'OPTS_0\[\] = \{(.*)\};', open(sys.argv[3]).read())
        names = re.findall(r'"([^"]*)"', m.group(1)) if m else []
    if len(a) != len(b):
        print("FAIL: sizes differ (%d vs %d samples)" % (len(a), len(b)))
        return 1
    n = len(a) // (FRAMES * 2)
    bad = 0
    print("%-18s %8s %8s %10s" % ("model", "a dBFS", "b dBFS", "diff dB"))
    for m in range(n):
        s = slice(m * FRAMES * 2, (m + 1) * FRAMES * 2)
        xa, xb = a[s], b[s]
        if any(v != v or abs(v) == float("inf") for v in xa) or any(v != v or abs(v) == float("inf") for v in xb):
            print("%-18s NaN/Inf" % (names[m] if m < len(names) else m))
            bad += 1
            continue
        ra, rb = rms(xa), rms(xb)
        rd = rms([p - q for p, q in zip(xa, xb)])
        rel = rd / max(ra, rb) if max(ra, rb) > 0 else 0.0
        flag = ""
        if (ra < 1e-5) != (rb < 1e-5) or (max(ra, rb) > 1e-5 and abs(20 * math.log10(max(ra, 1e-9) / max(rb, 1e-9))) > 3):
            flag = "  <-- level differs"
            bad += 1
        print("%-18s %s %s %s%s" % (names[m] if m < len(names) else m, db(ra), db(rb),
                                    "identical" if rd == 0 else db(rel) + " rel", flag))
    print("%d models, %d with a level mismatch" % (n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
