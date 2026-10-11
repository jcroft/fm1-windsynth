#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The vendored Plaits / stmlib subset is upstream's, unmodified: every file in
firmware/third_party/mutable/{plaits,stmlib} has the hash in MANIFEST.sha256, and nothing is
missing or extra (see that directory's README.md)."""
import hashlib
import sys
from pathlib import Path

D = Path(__file__).resolve().parents[1] / "firmware" / "third_party" / "mutable"
want = {}
for ln in (D / "MANIFEST.sha256").read_text().splitlines():
    sha, name = ln.split(None, 1)
    want[name.lstrip("*")] = sha
have = {str(p.relative_to(D)): hashlib.sha256(p.read_bytes()).hexdigest()
        for sub in ("plaits", "stmlib") for p in sorted((D / sub).rglob("*")) if p.is_file()}
bad = [f"changed: {n}" for n in sorted(want) if n in have and have[n] != want[n]]
bad += [f"missing: {n}" for n in sorted(set(want) - set(have))]
bad += [f"not in the manifest: {n}" for n in sorted(set(have) - set(want))]
for b in bad:
    print(b)
print(f"{len(have)} vendored files" + (", all as upstream" if not bad else ""))
sys.exit(1 if bad else 0)
