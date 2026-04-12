#!/usr/bin/env python3
"""Auto-increment build number and generate build_number.h."""
import os

script_dir = os.path.dirname(os.path.abspath(__file__))
txt = os.path.join(script_dir, "build_number.txt")
hdr = os.path.join(script_dir, "main", "build_number.h")

# Read and increment
n = 0
if os.path.exists(txt):
    with open(txt) as f:
        try:
            n = int(f.read().strip())
        except ValueError:
            n = 0
n += 1

# Write back
with open(txt, "w") as f:
    f.write(f"{n}\n")

# Generate header
with open(hdr, "w") as f:
    f.write(f"#pragma once\n#define BUILD_NUMBER {n}\n")

print(f"Build #{n}")
