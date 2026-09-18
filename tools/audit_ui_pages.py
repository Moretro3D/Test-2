#!/usr/bin/env python3
"""Liste les textes litteraux visibles, regroupes par ecran de l'interface."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
lines = (root / "TamaPoke.ino").read_text(encoding="utf-8", errors="replace").splitlines()
function = "global"
depth = 0
visible = False
rows = []

for number, line in enumerate(lines, 1):
    start = re.match(r"void\s+((?:render|draw|show)\w+)\s*\(", line)
    if start:
        function = start.group(1)
        depth = 0
        visible = True
    if not visible:
        continue
    depth += line.count("{") - line.count("}")
    for literal in re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', line):
        if not re.search(r"[A-Za-zÀ-ÿ]{2}", literal):
            continue
        if literal.startswith("%") or literal.startswith("/"):
            continue
        rows.append((function, number, literal))
    if depth == 0 and "{" in line:
        visible = False

last = None
for function, number, literal in rows:
    if function != last:
        print(f"\n[{function}]")
        last = function
    print(f"  L{number}: {literal}")

print(f"\nTOTAL: {len(rows)} textes litteraux a examiner")
