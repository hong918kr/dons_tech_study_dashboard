#!/bin/sh
# N07F: ethtool -S snapshot diff + diagnoser self-test over mock snapshot pairs.
# Python stdlib only; -I = isolated mode. Writes run_output.txt next to this file.
cd "$(dirname "$0")" || exit 1
python3 -I ethdiag.py --selftest cases 2>&1 | tee run_output.txt
