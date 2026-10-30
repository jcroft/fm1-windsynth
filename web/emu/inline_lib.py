#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Put the page's modules into it as plain declarations inside its <script type="module">s, so the page
stays one file: the update protocol (web/fm1pkg.js, web/fm1ota.js: Felucca's) where it says /*FM1LIB*/,
the sample banks (web/fm1samples.js) where it says /*SMPLIB*/.

  web/emu/inline_lib.py IN.html OUT.html"""
import re
import sys
from pathlib import Path

WEB = Path(__file__).resolve().parents[1]
LIBS = {"/*FM1LIB*/": ("fm1pkg.js", "fm1ota.js"), "/*SMPLIB*/": ("fm1samples.js",)}


def lib(names):
    out = []
    for name in names:
        src = (WEB / name).read_text()
        if re.search(r"^\s*import\b", src, re.M):
            raise SystemExit(f"inline_lib: {name} imports something: inline that too")
        out.append(f"/* ---- web/{name} (inlined) ---- */\n" + re.sub(r"^export\s+", "", src, flags=re.M))
    return "\n".join(out)


def main(src, dst):
    page = Path(src).read_text()
    for mark, names in LIBS.items():
        if page.count(mark) != 1:
            raise SystemExit(f"inline_lib: the page has no {mark} (or more than one)")
        page = page.replace(mark, lib(names).replace("</script", "<\\/script"))
    Path(dst).write_text(page)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
