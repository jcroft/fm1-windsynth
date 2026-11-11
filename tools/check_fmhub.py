#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""fmhub.json: the card The Firmware Hub (https://thefirmwarehub.com/kb/#fmhub) shows for us, checked
against its rules, so an edit that the hub would refuse fails here first.

  what     plain words, no links or markup, up to 300 characters
  author   2 to 60 characters, no links
  kinds    from synth groove drums chords tracker other
  play editor cheat   https:// addresses
  image    a square PNG, JPEG or WebP in the repository, 240 to 1024 px, 300 KB at most
  color    #RRGGBB, not too dark for the hub's dark page

  python3 tools/check_fmhub.py [fmhub.json]
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYS = {"what", "author", "kinds", "play", "editor", "cheat", "image", "color"}
KINDS = {"synth", "groove", "drums", "chords", "tracker", "other"}
LINKISH = re.compile(r"https?:|www\.|<|>|`|\b[a-z0-9-]+\.(com|org|net|io|dev)\b", re.I)
errors = []


def ok(cond, what):
    print(f"  {'ok  ' if cond else 'FAIL'} {what}")
    if not cond:
        errors.append(what)


def image_size(data):
    """(kind, width, height) of a PNG, JPEG or WebP, or None"""
    try:
        return _image_size(data)
    except (struct.error, IndexError):   # cut short
        return None


def _image_size(data):
    if data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR":
        return ("png",) + struct.unpack(">II", data[16:24])
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        c = data[12:16]
        if c == b"VP8 ":
            w, h = struct.unpack("<HH", data[26:30])
            return ("webp", w & 0x3FFF, h & 0x3FFF)
        if c == b"VP8L":
            b = int.from_bytes(data[21:25], "little")
            return ("webp", (b & 0x3FFF) + 1, ((b >> 14) & 0x3FFF) + 1)
        if c == b"VP8X":
            return ("webp", int.from_bytes(data[24:27], "little") + 1, int.from_bytes(data[27:30], "little") + 1)
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                return None
            m, n = data[i + 1], struct.unpack(">H", data[i + 2:i + 4])[0]
            if 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
                h, w = struct.unpack(">HH", data[i + 5:i + 9])
                return ("jpeg", w, h)
            i += 2 + n
    return None


def check(path):
    print(path)
    d = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(d, dict):
        ok(False, "an object of fields")
        return
    ok(set(d) <= KEYS, f"keys {sorted(d)} (known: {sorted(KEYS)})")
    what = d.get("what", "")
    if not isinstance(what, str):
        ok(False, "what: text")
        what = ""
    ok(0 < len(what) <= 300, f"what: {len(what)} characters (1 to 300)")
    ok(not LINKISH.search(what), "what: plain words, no links, addresses or markup")
    if "author" in d:
        a = d["author"]
        ok(isinstance(a, str) and 2 <= len(a) <= 60 and not LINKISH.search(a), f"author {a!r}: 2 to 60 characters, no links")
    k = d.get("kinds", [])
    ok(isinstance(k, list) and k and all(isinstance(x, str) and x in KINDS for x in k), f"kinds {k} (from {sorted(KINDS)})")
    for key in ("play", "editor", "cheat"):
        if key in d:
            ok(isinstance(d[key], str) and re.fullmatch(r"https://[^\s]+", d[key]) is not None, f"{key}: an https:// address")
    if "image" in d:
        rel = d["image"]
        img = (ROOT / rel).resolve() if isinstance(rel, str) and rel and not rel.startswith("/") else None
        inside = img is not None and img.is_relative_to(ROOT) and img.is_file()
        ok(inside, f"image {rel!r}: a file in the repository, by its path from the root")
        if inside:
            data = img.read_bytes()
            sz = image_size(data)
            ok(sz is not None, f"image: PNG, JPEG or WebP ({sz[0] if sz else 'unknown'})")
            if sz:
                ok(sz[1] == sz[2] and 240 <= sz[1] <= 1024, f"image: {sz[1]} x {sz[2]} (square, 240 to 1024)")
            ok(len(data) <= 300 * 1024, f"image: {len(data)} B (300 KB at most)")
    if "color" in d:
        c = d["color"]
        m = re.fullmatch(r"#([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})", c) if isinstance(c, str) else None
        ok(m is not None, f"color {c!r}: #RRGGBB")
        if m:
            r, g, b = (int(x, 16) for x in m.groups())
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            ok(luma >= 80, f"color {c}: brightness {luma:.0f} (80 or more, for the hub's dark page)")


if __name__ == "__main__":
    check(sys.argv[1] if len(sys.argv) > 1 else ROOT / "fmhub.json")
    if errors:
        sys.exit(f"fmhub.json: {len(errors)} problem(s)")
    print("fmhub.json: ok")
