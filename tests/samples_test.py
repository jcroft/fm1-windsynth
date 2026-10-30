#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Jeff Croft
"""tools/fm1_samples.py: WAVs read (PCM widths, float, stereo, the smpl chunk), banks built and checked,
mu-law, the SysEx framing; then the fixtures the scenarios load (tests/scenarios/sample.scn) into OUTDIR.

  tests/samples_test.py OUTDIR
"""
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import fm1_samples as fs  # noqa: E402

FAILS = 0


def check(ok, what):
    global FAILS
    if not ok:
        print("FAIL", what)
        FAILS += 1


def wav(path, x, rate, bits=16, ch=1, tag=1, smpl=None):
    """x: -1..1 frames (the same on each channel)"""
    w = bits // 8
    data = bytearray()
    for v in x:
        for _ in range(ch):
            if tag == 3:
                data += struct.pack("<f", v)
            elif bits == 8:
                data.append(max(0, min(255, int(round(v * 127)) + 128)))
            else:
                data += max(-(1 << bits - 1), min((1 << bits - 1) - 1, int(round(v * ((1 << bits - 1) - 1))))) \
                    .to_bytes(w, "little", signed=True)
    fmt = struct.pack("<HHIIHH", tag, ch, rate, rate * ch * w, ch * w, bits)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if smpl:
        root, fine, ls, le = smpl
        body = struct.pack("<IIIIIIIII", 0, 0, 0, root, int(fine / 100 * 4294967296), 0, 0, 1, 0)
        body += struct.pack("<IIIIII", 0, 0, ls, le - 1, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(body)) + body
    chunks += b"data" + struct.pack("<I", len(data)) + bytes(data) + (b"\0" if len(data) & 1 else b"")
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


def sine(hz, rate, n, amp=0.5):
    return [amp * math.sin(2 * math.pi * hz * k / rate) for k in range(n)]


def main(out):
    os.makedirs(out, exist_ok=True)
    # mu-law: G.711's table, both ways (the firmware decodes as ws_init does)
    def ulaw_dec(u):
        u = ~u & 0xFF
        e, m = (u >> 4) & 7, u & 15
        v = (((m << 3) + 0x84) << e) - 0x84
        return (-v if u & 0x80 else v) / 32768
    check(fs.ulaw_byte(0.0) == 0xFF, "mu-law of 0 is 0xFF")
    check(all(abs(ulaw_dec(fs.ulaw_byte(v / 100)) - v / 100) <= 0.033 * max(abs(v / 100), 0.01)
              for v in range(-100, 101)), "mu-law round trip")
    check(all(fs.ulaw_byte(ulaw_dec(u)) == u for u in range(256) if u not in (0x7F,)), "mu-law codes stable")

    # pack7 / message framing
    blob = bytes(range(256)) * 2
    check(fs.unpack7(fs.pack7(blob))[:len(blob)] == blob, "pack7 round trip")
    m = fs.message(fs.WRITE, b"\x40\0\0\0" + blob[:256])
    check(all(b < 0x80 for b in m[1:-1]) and m[0] == 0xF0 and m[-1] == 0xF7, "7-bit message")
    body = fs.unpack7(m[6:-1])
    check(body[-1] == ~sum(body[:-1]) & 0xFF, "check byte")
    reply = bytes([0xF0, 0x7D, 0x57, 0x53, 0x01, 0x44]) + fs.pack7(b"\0\x40\0\0\0\xBF") + b"\xF7"
    check(fs.parse_reply(reply, fs.WRITE) == b"\0\x40\0\0\0", "a reply parsed")
    check(fs.parse_reply(reply, fs.END) is None, "a reply to another command isn't")

    # WAVs: every width, float, stereo, smpl
    x = sine(441, 44100, 1000)
    for bits, tag in ((8, 1), (16, 1), (24, 1), (32, 1), (32, 3)):
        p = os.path.join(out, f"w{bits}_{tag}.wav")
        wav(p, x, 44100, bits=bits, ch=2, tag=tag)
        rate, y, root, fine, loop = fs.read_wav(p)
        tol = 0.02 if bits == 8 else 1e-4
        check(rate == 44100 and len(y) == 1000 and max(abs(a - b) for a, b in zip(x, y)) < tol,
              f"{bits}-bit format {tag} WAV read")
        check(root is None and loop is None, f"{bits}-bit: no smpl, no root or loop")
    p = os.path.join(out, "smpl.wav")
    wav(p, x, 44100, smpl=(62, 25, 100, 900))
    _, _, root, fine, loop = fs.read_wav(p)
    check(root == 62 and fine == 25 and loop == (100, 900), f"smpl chunk: root {root} fine {fine} loop {loop}")

    # resampling halves the frames and keeps the pitch
    y = fs.resample(sine(441, 44100, 4410), 44100, 22050)
    cross = sum(1 for a, b in zip(y[100:-100], y[101:-100]) if a < 0 <= b)
    check(len(y) == 2205 and abs(cross - 441 * (len(y) - 201) / 22050) <= 1, f"resample: {len(y)}, {cross} cycles")

    # a bank: what the firmware checks (wind_sample.c's entry_ok)
    a = os.path.join(out, "a440.wav")            # 440 Hz at 44.1 kHz: 100 periods, 10022.7 frames; loop 20 periods
    wav(a, sine(440, 44100, 10023), 44100, smpl=(69, 0, 0, 2005))
    b = os.path.join(out, "lo.wav")               # 220 Hz at 22.05 kHz, as mu-law, its own root
    wav(b, sine(220, 22050, 5000, 0.8), 22050)
    slots = [fs.make_slot(o) for o in (fs.parse_spec(a + ",name=a440"),
                                       fs.parse_spec(b + ",fmt=ulaw,root=57,name=lo tone"))]
    bank = fs.build([slots[0], None, slots[1]])
    e = dict(fs.entries(bank))
    check(sorted(e) == [1, 3], f"slots {sorted(e)}")
    check(e[1]["name"] == "A440" and e[1]["root"] == 69 and e[1]["le"] == 2005 and e[1]["fmt"] == fs.FMT_S16,
          f"slot 1 {e[1]}")
    check(e[3]["name"] == "LO TONE" and e[3]["root"] == 57 and e[3]["fmt"] == fs.FMT_ULAW and e[3]["le"] == 0,
          f"slot 3 {e[3]}")
    check(e[1]["off"] == fs.DATA and e[3]["off"] % 2 == 0, "offsets")
    check(struct.unpack_from("<I", bank, 12)[0] == zlib.crc32(bank[64:]), "CRC")
    bad = bytearray(bank)
    bad[-1] ^= 1
    try:
        fs.entries(bytes(bad))
        check(False, "a bad CRC is caught")
    except fs.SampleError:
        pass
    try:
        fs.build([slots[0]] * 30)
        check(False, "a bank too big for the FM-1 is refused")
    except fs.SampleError:
        pass
    p = os.path.join(out, "short_loop.wav")     # a loop too short to survive 44.1 to 8 kHz: refused, not kept
    wav(p, x, 44100, smpl=(60, 0, 500, 502))
    try:
        fs.make_slot(fs.parse_spec(p + ",rate=8000"))
        check(False, "a loop that collapses when resampled is refused")
    except fs.SampleError:
        pass
    p = os.path.join(out, "sharp.wav")          # 99.6 cents above 61: 62
    wav(p, x, 44100, smpl=(61, 99.6, 0, 900))
    _, _, root, fine, _ = fs.read_wav(p)
    check(root == 62 and fine == 0, f"a hair under 62 is 62 (got {root} {fine})")
    for spec in (a + ",loop=5:6", a + ",root=200", a + ",bogus=1", a + ",fmt=mp3", a + ",root=x", a + ",loop=a:b"):
        try:
            fs.make_slot(fs.parse_spec(spec))
            check(False, f"{spec} refused")
        except fs.SampleError:
            pass

    # the fixtures: the bank, its SysEx, and one too big for a message (the loader refuses it)
    with open(os.path.join(out, "bank.bin"), "wb") as f:
        f.write(bank)
    ms = fs.frames(bank)
    check(len(ms) == 2 + -(-(len(bank) - 64) // fs.PIECE), "frames: BEGIN, the pieces, END")
    with open(os.path.join(out, "load.txt"), "w") as f:
        for m in ms:
            f.write(" ".join(f"{v:02X}" for v in m) + "\n")
    with open(os.path.join(out, "end_again.txt"), "w") as f:    # END sent again, as after a lost reply
        f.write(" ".join(f"{v:02X}" for v in ms[-1]) + "\n")
    if FAILS:
        print(f"{FAILS} failed")
        return 1
    print("samples tool ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/samples"))
