#!/usr/bin/env python3
"""Offline checks for the aarch64 build (called by build-aarch64.sh). Standard library only.

    check_aarch64.py <plugin folder> <params.h> [<released armv7 plaits.so>]

1. plaits.so is ELF64 little-endian aarch64, a shared object, exports exactly VSTPluginMain, needs only
   libc/libm/libpthread/libdl (+ the loader), and no glibc symbol version above 2.39 (the device's).
2. Every "Parameter N" the skin binds to exists in params.h.
3. With the released armv7 binary given: its parameter table (found in the binary) equals params.h, i.e. the
   released skin and saved projects fit this build: key, name, unit, min, max, default and option count always, and
   every other field (momentary, display kind, step_of, popup_of, ...) when params.h has the release's param_t.
"""
import os
import re
import struct
import sys

MAX_GLIBC = (2, 39)
ALLOWED_LIBS = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "ld-linux-aarch64.so.1"}


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def elf64(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        fail("%s is not an ELF file" % path)
    if d[4] != 2 or d[5] != 1:
        fail("not ELF64 little-endian (class %d, data %d)" % (d[4], d[5]))
    e_type, e_machine = struct.unpack_from("<HH", d, 16)
    if e_type != 3:
        fail("not a shared object (e_type %d)" % e_type)
    if e_machine != 183:
        fail("not aarch64 (e_machine %d)" % e_machine)
    e_shoff, = struct.unpack_from("<Q", d, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x3A)
    secs = []
    for i in range(e_shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", d, e_shoff + i * e_shentsize)
        secs.append(dict(name=name, type=typ, off=off, size=size, link=link, info=info, entsize=entsize))
    strtab = secs[e_shstrndx]

    def s(tab, o):
        b = tab["off"] + o
        return d[b:d.index(b"\0", b)].decode()
    for x in secs:
        x["n"] = s(strtab, x["name"])
    by = {x["n"]: x for x in secs}
    dynstr = by[".dynstr"]
    exported, needed, versions = [], [], set()
    dynsym = by[".dynsym"]
    for i in range(1, dynsym["size"] // 24):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", d, dynsym["off"] + i * 24)
        if st_shndx != 0 and (st_info >> 4) in (1, 2):   # defined, GLOBAL or WEAK
            exported.append(s(dynstr, st_name))
    dyn = by[".dynamic"]
    for i in range(dyn["size"] // 16):
        tag, val = struct.unpack_from("<qQ", d, dyn["off"] + i * 16)
        if tag == 1:
            needed.append(s(dynstr, val))
    vr = by.get(".gnu.version_r")
    if vr:
        o = vr["off"]
        for _ in range(vr["info"]):
            ver, cnt, file_, aux, nxt = struct.unpack_from("<HHIII", d, o)
            a = o + aux
            for _ in range(cnt):
                h, fl, other, name, anext = struct.unpack_from("<IHHII", d, a)
                versions.add(s(dynstr, name))
                a += anext
            o += nxt
    return exported, needed, versions


TOKEN = re.compile(r'"((?:[^"\\\\]|\\\\.)*)"|([^,\s][^,]*)')


def params_rows(path):
    """params.h PARAMS rows as token lists: strings as str, numbers as float, option tables as their OPTS_n name."""
    txt = open(path).read()
    opts = {m.group(1): len(TOKEN.findall(m.group(2))) for m in re.finditer(r'(OPTS_\d+)\[\] = \{(.*)\};', txt)}
    out = []
    for r in re.findall(r'^    \{(.*)\},$', txt, re.M):
        row = []
        for m in TOKEN.finditer(r):
            if m.group(1) is not None:
                row.append(m.group(1).replace('\\"', '"').replace("\\\\", "\\"))
            else:
                t = m.group(2).strip()
                row.append(t if t.startswith("OPTS_") else float(t.rstrip("f")))
        out.append(row)
    return out, opts


def armv7_table(path, rows):
    """The PARAMS table of the released 32-bit binary, decoded with the shape of params.h's rows (a string where params.h
    has one, else a float or an int as params.h's token says). Returns (table, entry size in words, key after the table)."""
    d = open(path, "rb").read()
    e_phoff, = struct.unpack_from("<I", d, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(e_phnum):
        p = struct.unpack_from("<8I", d, e_phoff + i * e_phentsize)
        if p[0] == 1:
            segs.append((p[2], p[1], p[4]))

    def cstr(v):
        for va, o, fs in segs:
            if v and va <= v < va + fs:
                b = o + (v - va)
                e = d.find(b"\0", b, b + 200)
                return d[b:e].decode("utf-8", "replace") if e >= 0 else None
        return None
    keys = [r[0] for r in rows]
    for o in range(0, len(d) - 4, 4):
        if cstr(struct.unpack_from("<I", d, o)[0]) != keys[0]:
            continue
        for size in range(32, 200, 4):
            if o + size + 4 > len(d) or cstr(struct.unpack_from("<I", d, o + size)[0]) != keys[1]:
                continue
            table = []
            for i in range(len(rows)):
                b = o + i * size
                words = struct.unpack_from("<%dI" % (size // 4), d, b)
                floats = struct.unpack_from("<%df" % (size // 4), d, b)
                ints = struct.unpack_from("<%di" % (size // 4), d, b)
                table.append((words, floats, ints, cstr))
            end = o + len(rows) * size
            after = cstr(struct.unpack_from("<I", d, end)[0]) if end + 4 <= len(d) else None
            return table, size // 4, after
    return None, 0, None


FLOAT_FIELDS = {3, 4, 5, 12}   # min, max, def, step_delta (param_t in params.h)


def compare(rows, opts, table, nwords):
    """-> list of differences between params.h and the released binary's table, field by field."""
    diffs = []
    for i, (row, (words, floats, ints, cstr)) in enumerate(zip(rows, table)):
        full = len(row) == nwords          # same param_t as the release: compare every field
        for f, mine in enumerate(row if full else row[:7]):
            if isinstance(mine, str) and mine.startswith("OPTS_"):
                continue                    # the option strings are checked through nopts + the display test on the device
            if isinstance(mine, str):
                theirs = cstr(words[f]) or ""
                ok = theirs == mine
            elif f in FLOAT_FIELDS:
                theirs = floats[f]
                ok = abs(theirs - mine) <= 1e-6
            else:
                theirs = ints[f]
                ok = theirs == int(mine)
            if not ok:
                diffs.append("param %d %s field %d: %r here, %r in the release" % (i, row[0], f, mine, theirs))
    return diffs


def main():
    folder, params_h = sys.argv[1], sys.argv[2]
    so = os.path.join(folder, "plaits.so")
    exported, needed, versions = elf64(so)
    if exported != ["VSTPluginMain"]:
        fail("exports %r, expected only VSTPluginMain" % exported)
    extra = set(needed) - ALLOWED_LIBS
    if extra:
        fail("needs unexpected libraries: %s" % ", ".join(sorted(extra)))
    glibc = sorted(tuple(int(x) for x in v[6:].split(".")) for v in versions if v.startswith("GLIBC_"))
    other = sorted(v for v in versions if not v.startswith("GLIBC_"))
    if other:
        fail("non-glibc symbol versions: %s" % ", ".join(other))
    if glibc and glibc[-1][:2] > MAX_GLIBC:
        fail("needs glibc %s (device has %d.%d)" % (".".join(map(str, glibc[-1])), *MAX_GLIBC))
    print("ok: ELF64 aarch64 shared object, exports VSTPluginMain only, needs %s, highest glibc %s"
          % (" ".join(needed), ".".join(map(str, glibc[-1])) if glibc else "none"))

    rows, opts = params_rows(params_h)
    n = int(re.search(r"#define NPARAMS (\d+)", open(params_h).read()).group(1))
    if len(rows) != n or len({len(r) for r in rows}) != 1:
        fail("params.h: parsed %d rows, NPARAMS is %d" % (len(rows), n))
    tui = open(os.path.join(folder, "Plugin Skins", "TUI.json")).read()
    refs = {int(x) for x in re.findall(r"Parameter (\d+)", tui)}
    if not refs or max(refs) >= n:
        fail("skin binds Parameter %s, the build has 0..%d" % (max(refs) if refs else "-", n - 1))
    print("ok: skin binds %d distinct parameters (0..%d); the build has %d" % (len(refs), max(refs), n))

    if len(sys.argv) > 3:
        table, nwords, after = armv7_table(sys.argv[3], rows)
        if table is None:
            fail("parameter table not found in the released binary")
        keys = [r[0] for r in rows]
        if after in keys or (after and after.endswith("__open")):
            fail("the released binary has more parameters than this build")
        diffs = compare(rows, opts, table, nwords)
        if diffs:
            fail("parameter table differs from the released binary:\n  " + "\n  ".join(diffs[:20]))
        if len(rows[0]) == nwords:
            print("ok: all %d parameters match the released armv7 binary in every field (%d fields each: same param_t, "
                  "same wrapper generation)" % (n, nwords))
        else:
            print("ok: all %d parameters match the released armv7 binary (key, name, unit, min, max, default, option count); "
                  "param_t has %d fields here and %d in the release: a different wrapper revision" % (n, len(rows[0]), nwords))


if __name__ == "__main__":
    main()
