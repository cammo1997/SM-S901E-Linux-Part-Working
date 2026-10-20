#!/usr/bin/env python3
"""Dump a Qualcomm CamX "QTI Chromatix" parameter binary (Parameter Parser V3.x), e.g. a sensormodule .bin.
Layout (reverse engineered from S22 vendor files): sections table at 0xA4 = {count, (id, offset, size) x count};
section 0 = 56-byte records {u32 id, char name[32], u32 kind, u32 a, u32 b, u32 data_off, u32 data_size};
section 1 = data blob that data_off/data_size index."""
import struct, sys

def load(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<I", d, 0xA4)[0]
    secs = {}
    for i in range(n):
        sid, off, size = struct.unpack_from("<3I", d, 0xA8 + 12 * i)
        secs[sid] = (off, size)
    off0, size0 = secs[0]
    data_off = secs[1][0]
    recs = []
    for p in range(off0, off0 + size0 - 55, 56):
        rid, name, kind, a, b, doff, dsz = struct.unpack_from("<I32s5I", d, p)
        name = name.split(b"\0")[0].decode("ascii", "replace")
        recs.append((rid, name, kind, a, b, doff, dsz, d[data_off + doff:data_off + doff + dsz]))
    return recs

if __name__ == "__main__" and "--c" not in sys.argv:
    for rid, name, kind, a, b, doff, dsz, raw in load(sys.argv[1]):
        if dsz == 0 and kind == 0:
            continue
        txt = raw.rstrip(b"\0").decode("ascii", "replace") if raw and all(32 <= c < 127 for c in raw.rstrip(b"\0")) else ""
        words = [struct.unpack_from("<I", raw, i)[0] for i in range(0, min(len(raw), 32) - 3, 4)]
        print(f"{rid:5d} {name:32s} k={kind} a={a} b={b:#x} off={doff} size={dsz} {repr(txt) if txt else ' '.join(f'{w:#x}' for w in words)}")


def reg_tables(recs):
    """Yield (label, [(addr, value, delay_us), ...]) for every regSetting array, values resolved through their records."""
    byid = {r[0]: r for r in recs}
    skip = {"slaveAddr", "registerData", "delayUs", "regSetting"}
    ctx, mode = "", 0
    for rid, name, kind, a, b, doff, dsz, raw in recs:
        if name == "modeName":
            pass
        if name == "regSetting":
            out = []
            for i in range(0, dsz - 39, 40):
                w = struct.unpack_from("<10I", raw, i)
                addr, data_rec, delay_rec = w[2], byid.get(w[4]), byid.get(w[9])
                val = struct.unpack_from("<I", data_rec[7])[0] if data_rec and data_rec[6] >= 4 else 0
                dly = struct.unpack_from("<I", delay_rec[7])[0] if delay_rec and delay_rec[6] >= 4 else 0
                out.append((addr, val, dly))
            yield ctx, out
        elif name not in skip:
            ctx = name


def emit_c(path, prefix):
    recs = load(path)
    modes = [r for r in recs if r[1] == "regSetting" and r[6] >= 4000]
    names = []
    for i, r in enumerate(recs):
        if r[1] == "modeName":
            names.append(recs[i + 1][7].split(b"\0")[0].decode())
    mi = 0
    for ctx, regs in reg_tables(recs):
        if len(regs) >= 100 and ctx == "integrationInfo":
            label = f"{prefix}_mode{mi}"
            title = names[mi] if mi < len(names) else ""
            mi += 1
        elif ctx == "initSettings":
            label, title = f"{prefix}_init", "initSettings"
        else:
            continue
        print(f"/* {title} ({len(regs)} writes) */\nstatic const struct cci_reg_sequence {label}[] = {{")
        for a, v, d in regs:
            print(f"\t{{ CCI_REG8({a:#06x}), {v:#04x} }},{f' /* delay {d} us */' if d else ''}")
        print("};\n")


if __name__ == "__main__" and len(sys.argv) > 3 and sys.argv[2] == "--c":
    emit_c(sys.argv[1], sys.argv[3])
