#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Jeff Croft
"""Sample banks for the Windsynth FM-1's sample layer (firmware/src/wind/wind_sample.h).

  fm1_samples.py build BANK.bin SPEC [SPEC ...]   a bank from WAV files, one SPEC a slot, in order
  fm1_samples.py show BANK.bin                    what's in a bank
  fm1_samples.py send BANK.bin [--port NAME]      load it on the FM-1 over USB-MIDI
  fm1_samples.py backup OUT.bin [--port NAME]     read the FM-1's bank back
  fm1_samples.py info [--port NAME]               the FM-1's bank: room, and what's in each slot
  fm1_samples.py frames BANK.bin OUT.txt          the SysEx messages `send` sends, as hex lines (tests)

A SPEC is a WAV file and options after commas, or "-" for an empty slot:

  sax.wav,root=62,loop=1200:30500,name=SAX,fmt=ulaw,rate=22050

  root=N       the note (MIDI) the sample is at; default the WAV's own (its smpl chunk), else 60
  fine=C       its tuning off that, in cents (-99..99); default the WAV's own, else 0
  loop=S:E     loop start and end, in frames of the WAV; default the WAV's own loop, else the whole
  rate=HZ      resample to HZ (4000..48000); default the WAV's rate (48000 at most)
  fmt=s16|ulaw 16-bit, or 8-bit mu-law (half the room, a little hiss); default s16
  name=TEXT    up to 8 characters; default from the file's name

WAVs: PCM 8/16/24/32-bit or 32-bit float, any channels (mixed to one). Needs mido with python-rtmidi
for send / backup / info.
"""
import argparse
import os
import re
import struct
import sys
import time
import zlib

MAGIC, VERSION, NSLOT = 0x504D5357, 1, 32
HDR, ENT, NAME = 64, 32, 8
DATA = HDR + NSLOT * ENT
CAP = 0x3D000                          # the FM-1's room (PLAT_SAMPLES_CAP)
FMT_NONE, FMT_S16, FMT_ULAW = 0, 1, 2
FMT_NAMES = {"s16": FMT_S16, "ulaw": FMT_ULAW}
PIECE = 256
INFO, READ, BEGIN, WRITE, END = 1, 2, 3, 4, 5
RC = ["OK", "bad message", "out of order", "flash error", "check failed"]
PORT_RE = re.compile(r"fm-1|windsynth", re.I)
TIMEOUT = 2.0                          # seconds for a reply (an erase takes ~50 ms)


class SampleError(Exception):
    pass


# ------------------------------------------------------------------ WAV ---

def read_wav(path):
    """(rate, mono samples -1..1, root or None, fine cents or None, (loop start, end) or None)"""
    with open(path, "rb") as f:
        b = f.read()
    if len(b) < 12 or b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise SampleError(f"{path}: not a WAV file")
    fmt = data = smpl = None
    i = 12
    while i + 8 <= len(b):
        cid, n = b[i:i + 4], struct.unpack_from("<I", b, i + 4)[0]
        body = b[i + 8:i + 8 + n]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        elif cid == b"smpl":
            smpl = body
        i += 8 + n + (n & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise SampleError(f"{path}: no fmt or data chunk")
    tag, ch, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
    if tag == 0xFFFE and len(fmt) >= 26:
        tag = struct.unpack_from("<H", fmt, 24)[0]
    if ch < 1 or align < 1 or (tag, bits) not in ((1, 8), (1, 16), (1, 24), (1, 32), (3, 32)):
        raise SampleError(f"{path}: {bits}-bit format {tag} isn't one this reads (PCM or 32-bit float)")
    width = bits // 8
    frames = len(data) // align
    out = []
    for k in range(frames):
        acc = 0.0
        for c in range(ch):
            o = k * align + c * width
            if tag == 3:
                v = struct.unpack_from("<f", data, o)[0]
            elif bits == 8:
                v = (data[o] - 128) / 128.0
            else:
                v = int.from_bytes(data[o:o + width], "little", signed=True) / float(1 << (bits - 1))
            acc += v
        out.append(acc / ch)
    root = fine = loop = None
    if smpl and len(smpl) >= 36:
        unity, frac = struct.unpack_from("<II", smpl, 12)
        nloops = struct.unpack_from("<I", smpl, 28)[0]
        root = unity if unity < 128 else None
        fine = round(frac / 4294967296.0 * 100)
        if nloops and len(smpl) >= 60:
            s, e = struct.unpack_from("<II", smpl, 44)
            if s < e < frames:
                loop = (s, e + 1)      # (smpl's end is the loop's last frame)
    return rate, out, root, fine, loop


def resample(x, src, dst):
    """windowed sinc, cut off below the lower Nyquist"""
    if src == dst:
        return list(x)
    import math
    ratio = dst / src
    cut = min(1.0, ratio) * 0.95
    taps = 16
    n = int(len(x) * ratio)
    out = []
    for k in range(n):
        t = k / ratio
        j0 = int(t)
        acc = 0.0
        for j in range(j0 - taps + 1, j0 + taps + 1):
            if 0 <= j < len(x):
                d = t - j
                w = 0.5 + 0.5 * math.cos(math.pi * d / taps) if abs(d) < taps else 0.0
                s = cut if d == 0 else math.sin(math.pi * cut * d) / (math.pi * d)
                acc += x[j] * s * w
        out.append(acc)
    return out


def ulaw_byte(v):
    """G.711 mu-law of -1..1"""
    s = max(-32635, min(32635, int(round(v * 32768))))
    sign = 0x80 if s < 0 else 0
    s = abs(s) + 0x84
    e = 7
    while e > 0 and not s & (0x4000 >> (7 - e)):
        e -= 1
    m = (s >> (e + 3)) & 15
    return ~(sign | e << 4 | m) & 0xFF


# ----------------------------------------------------------------- bank ---

def parse_spec(spec):
    if spec == "-":
        return None
    parts = spec.split(",")
    o = {"path": parts[0]}
    for p in parts[1:]:
        k, sep, v = p.partition("=")
        if not sep or k not in ("root", "fine", "loop", "rate", "fmt", "name"):
            raise SampleError(f"{spec}: what's {p!r}? (root= fine= loop=S:E rate= fmt= name=)")
        o[k] = v
    return o


def make_slot(o):
    """one slot's entry fields and audio bytes"""
    rate, x, root, fine, loop = read_wav(o["path"])
    if not x:
        raise SampleError(f"{o['path']}: no audio")
    root = int(o.get("root", root if root is not None else 60))
    fine = int(o.get("fine", fine if fine is not None else 0))
    if "loop" in o:
        s, _, e = o["loop"].partition(":")
        loop = (int(s), int(e))
    to = int(o.get("rate", min(rate, 48000)))
    fmt = FMT_NAMES.get(o.get("fmt", "s16"))
    if not 0 <= root <= 127 or not -99 <= fine <= 99 or not 4000 <= to <= 48000 or fmt is None:
        raise SampleError(f"{o['path']}: root 0..127, fine -99..99, rate 4000..48000, fmt s16 or ulaw")
    if loop and not (0 <= loop[0] and loop[0] + 2 <= loop[1] <= len(x)):
        raise SampleError(f"{o['path']}: the loop {loop[0]}:{loop[1]} isn't inside its {len(x)} frames")
    if to != rate:
        x = resample(x, rate, to)
        if loop:
            loop = (round(loop[0] * to / rate), min(len(x), round(loop[1] * to / rate)))
    if len(x) < 4:
        raise SampleError(f"{o['path']}: too short")
    if fmt == FMT_S16:
        audio = b"".join(struct.pack("<h", max(-32768, min(32767, int(round(v * 32767))))) for v in x)
    else:
        audio = bytes(ulaw_byte(v) for v in x)
    name = o.get("name", os.path.splitext(os.path.basename(o["path"]))[0])
    name = re.sub(r"[^ -~]", "", name)[:NAME].upper()
    ls, le = loop if loop else (0, 0)
    return dict(len=len(x), ls=ls, le=le, rate=to, fmt=fmt, root=root, fine=fine, name=name), audio


def build(slots):
    """the bank's bytes from up to NSLOT (entry, audio) pairs, None an empty slot"""
    if len(slots) > NSLOT:
        raise SampleError(f"{len(slots)} samples: the bank holds {NSLOT}")
    ents, data = b"", bytearray()
    for s in slots + [None] * (NSLOT - len(slots)):
        if s is None:
            ents += bytes(ENT)
            continue
        e, audio = s
        if len(data) & 1:
            data.append(0)             # (16-bit samples start on an even byte)
        off = DATA + len(data)
        data += audio
        ents += struct.pack("<IIIIHBBb3x8s", off, e["len"], e["ls"], e["le"], e["rate"], e["fmt"], e["root"],
                            e["fine"], e["name"].encode())
    body = ents + bytes(data)
    size = HDR + len(body)
    if size > CAP:
        raise SampleError(f"the bank is {size} bytes: the FM-1 has room for {CAP} (try fmt=ulaw, or a lower rate)")
    return struct.pack("<IHHII", MAGIC, VERSION, NSLOT, size, zlib.crc32(body)).ljust(HDR, b"\0") + body


def entries(bank):
    """[(slot, fields)] of a bank's slots holding a sample; SampleError if it isn't a good bank"""
    if len(bank) < DATA:
        raise SampleError("not a sample bank (too short)")
    magic, ver, n, size, crc = struct.unpack_from("<IHHII", bank)
    if magic != MAGIC or ver != VERSION or n != NSLOT or not DATA <= size <= len(bank):
        raise SampleError("not a sample bank (header)")
    if zlib.crc32(bank[HDR:size]) != crc:
        raise SampleError("the bank's CRC doesn't match")
    out = []
    for i in range(NSLOT):
        off, ln, ls, le, rate, fmt, root, fine, name = struct.unpack_from("<IIIIHBBb3x8s", bank, HDR + i * ENT)
        if fmt:
            out.append((i + 1, dict(off=off, len=ln, ls=ls, le=le, rate=rate, fmt=fmt, root=root, fine=fine,
                                    name=name.rstrip(b"\0").decode("ascii", "replace"))))
    return out


def describe(bank, out=sys.stdout):
    size = struct.unpack_from("<I", bank, 8)[0]
    note = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    for s, e in entries(bank):
        loop = f"loop {e['ls']}:{e['le']}" if e["le"] else "loop all"
        fine = f" {e['fine']:+d}c" if e["fine"] else ""
        print(f"  {s:2d} {e['name']:<8} {note[e['root'] % 12]}{e['root'] // 12 - 1}{fine}  {e['rate']} Hz "
              f"{'mu-law' if e['fmt'] == FMT_ULAW else '16-bit'}  {e['len'] / e['rate']:.2f} s  {loop}", file=out)
    print(f"  {size} of {CAP} bytes ({100 * size // CAP}%)", file=out)


# --------------------------------------------------------------- SysEx ---

def pack7(b):
    out, acc, nb = bytearray(), 0, 0
    for x in b:
        acc |= x << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = bytearray(), 0, 0
    for x in s:
        acc |= x << nb
        nb += 7
        if nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def message(cmd, body=b""):
    return bytes([0xF0, 0x7D, 0x57, 0x53, 0x01, cmd]) + pack7(body + bytes([~sum(body) & 0xFF])) + b"\xF7"


def parse_reply(pkt, cmd):
    """the reply's body (rc first) if pkt answers cmd, else None"""
    if len(pkt) < 8 or pkt[:5] != b"\xF0\x7D\x57\x53\x01" or pkt[5] != cmd | 0x40 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[6:-1])
    if not d or d[-1] != ~sum(d[:-1]) & 0xFF:
        return None
    return d[:-1]


def frames(bank):
    """the messages that load bank: BEGIN, the WRITEs, END"""
    size = len(bank)
    out = [message(BEGIN, struct.pack("<I", size))]
    for off in range(HDR, size, PIECE):
        out.append(message(WRITE, struct.pack("<I", off) + bank[off:off + PIECE]))
    out.append(message(END, bank[:HDR]))
    return out


class Link:
    """the FM-1's MIDI ports, one request and its reply at a time"""

    def __init__(self, port=None):
        try:
            import mido
        except ImportError:
            raise SampleError("needs mido with python-rtmidi (pip install mido python-rtmidi)")
        import queue
        self.q = queue.Queue()
        ins, outs = mido.get_input_names(), mido.get_output_names()
        want = (lambda n: port.lower() in n.lower()) if port else PORT_RE.search
        i = next((n for n in ins if want(n)), None)
        o = next((n for n in outs if want(n)), None)
        if not i or not o:
            raise SampleError("no FM-1 found (ports: " + ", ".join(ins) + ")")
        self.mido = mido
        self.inp = mido.open_input(i, callback=lambda m: m.type == "sysex" and
                                   self.q.put(bytes([0xF0, *m.data, 0xF7])))
        self.out = mido.open_output(o)
        time.sleep(0.3)                # (CoreMIDI may drop what's sent just after opening)

    def ask(self, pkt, tries=3):
        cmd = pkt[5]
        for _ in range(tries):
            while not self.q.empty():
                self.q.get_nowait()
            self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            end = time.monotonic() + TIMEOUT
            while (left := end - time.monotonic()) > 0:
                try:
                    r = parse_reply(self.q.get(timeout=left), cmd)
                except Exception:
                    break
                if r is not None:
                    if r[0]:
                        raise SampleError(f"the FM-1 said: {RC[r[0]] if r[0] < len(RC) else r[0]}")
                    return r[1:]
        raise SampleError("no answer from the FM-1 (is it running the Windsynth firmware, with the sample layer?)")

    def close(self):
        self.inp.close()
        self.out.close()


def info(link):
    r = link.ask(message(INFO))
    room, size, nslot = struct.unpack_from("<IIH", r)
    return room, size


def read_bank(link, size, progress=lambda f: None):
    b = bytearray()
    while len(b) < size:
        n = min(PIECE, size - len(b))
        r = link.ask(message(READ, struct.pack("<IH", len(b), n)))
        b += r[4:4 + n]
        progress(len(b) / size)
    return bytes(b)


def send(link, bank, progress=lambda f: None):
    room, _ = info(link)
    if len(bank) > room:
        raise SampleError(f"the bank is {len(bank)} bytes: the FM-1 has room for {room}")
    msgs = frames(bank)
    for k, m in enumerate(msgs):
        link.ask(m)
        progress((k + 1) / len(msgs))


def bar(f):
    sys.stdout.write(f"\r  {int(f * 100):3d}%")
    sys.stdout.flush()
    if f >= 1:
        sys.stdout.write("\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("build")
    p.add_argument("bank")
    p.add_argument("spec", nargs="+")
    sub.add_parser("show").add_argument("bank")
    for c in ("send", "backup"):
        p = sub.add_parser(c)
        p.add_argument("bank")
        p.add_argument("--port")
    sub.add_parser("info").add_argument("--port")
    p = sub.add_parser("frames")
    p.add_argument("bank")
    p.add_argument("out")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "build":
            bank = build([None if o is None else make_slot(o) for o in map(parse_spec, a.spec)])
            with open(a.bank, "wb") as f:
                f.write(bank)
            describe(bank)
        elif a.cmd in ("show", "frames", "send"):
            with open(a.bank, "rb") as f:
                bank = f.read()
            entries(bank)
            if a.cmd == "show":
                describe(bank)
            elif a.cmd == "frames":
                with open(a.out, "w") as f:
                    for m in frames(bank):
                        f.write(" ".join(f"{x:02X}" for x in m) + "\n")
            else:
                link = Link(a.port)
                try:
                    print("  loading (the sound stops while the flash is written)")
                    send(link, bank, bar)
                finally:
                    link.close()
                print("  loaded")
        else:
            link = Link(a.port)
            try:
                room, size = info(link)
                if not size:
                    print(f"  no samples on the FM-1 ({room} bytes of room)")
                    return 0 if a.cmd == "info" else 1
                bank = read_bank(link, min(size, DATA) if a.cmd == "info" else size,
                                 bar if a.cmd == "backup" else lambda f: None)
            finally:
                link.close()
            if a.cmd == "backup":
                entries(bank)
                with open(a.bank, "wb") as f:
                    f.write(bank)
                describe(bank)
            else:
                for s in range(NSLOT):
                    e = struct.unpack_from("<IIIIHBBb3x8s", bank, HDR + s * ENT)
                    if e[5]:
                        name = e[8].rstrip(b"\0").decode("ascii", "replace")
                        print(f"  {s + 1:2d} {name:<8} {e[1] / e[4]:.2f} s")
                print(f"  {size} of {room} bytes")
    except (SampleError, OSError) as e:
        print(f"fm1_samples: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
