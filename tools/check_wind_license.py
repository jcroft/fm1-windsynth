#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""firmware/src/wind/ is MPL-2.0 (see LICENSING.md): every file there says so, and none includes
anything from outside the directory except the maths seam's dsp/fastmath.h (wind_math.h, which
-DWIND_MATH_LIBM replaces), the vendored Mutable code (plaits/, stmlib/) and the C library."""
import re
import sys
from pathlib import Path

D = Path(__file__).resolve().parents[1] / "firmware" / "src" / "wind"
bad = []
files = [p for p in sorted(D.rglob("*")) if p.is_file() and p.name != "LICENSE"]
for p in files:
    name = str(p.relative_to(D))
    text = p.read_text()
    if text.splitlines()[0] != "/* SPDX-License-Identifier: MPL-2.0 */":
        bad.append(f"not MPL-2.0: {name}")
    for inc in re.findall(r'^\s*#\s*include\s+"([^"]+)"', text, re.M):
        if name == "wind_math.h" and inc == "../dsp/fastmath.h":
            continue
        if inc.startswith(("plaits/", "stmlib/")):
            continue
        if not (p.parent / inc).resolve().is_relative_to(D):
            bad.append(f"includes outside firmware/src/wind/: {name}: {inc}")
        elif not (p.parent / inc).resolve().exists():
            bad.append(f"includes a missing file: {name}: {inc}")
for b in bad:
    print(b)
print(f"{len(files)} wind files" + (", all MPL-2.0 and self-contained" if not bad else ""))
sys.exit(1 if bad else 0)
