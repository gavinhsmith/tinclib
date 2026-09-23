#!/usr/bin/env python3
"""Fails unless the minimal program is smaller than the full one by a margin.

    python tools/size_check.py MIN.8xp FULL.8xp [min_saving_bytes]

size_min calls only tinc_init/tinc_isActive; hello also uses the whole
request API. If unused functions were linked in anyway the two would be
about the same size.
"""

import os
import sys

small, full = sys.argv[1], sys.argv[2]
margin = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
s, f = os.path.getsize(small), os.path.getsize(full)
print(f"{small}: {s} bytes\n{full}: {f} bytes\nsaving: {f - s} bytes (need >= {margin})")
sys.exit(0 if f - s >= margin else 1)
