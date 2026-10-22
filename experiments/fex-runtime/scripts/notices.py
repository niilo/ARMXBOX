#!/usr/bin/env python3
"""Bundle license texts from the pinned source alongside the diagnostic binary."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
paths = ['LICENSE', 'External/fmt/LICENSE', 'External/rpmalloc/LICENSE',
         'External/unordered_dense/LICENSE', 'External/xxhash/LICENSE',
         'External/cephes/LICENSE', 'External/SoftFloat-3e/include/SoftFloat-3e/softfloat.h']
Path(sys.argv[2]).write_text('\n\n'.join(f'===== {p} =====\n{(root / p).read_text()}' for p in paths))
