#!/usr/bin/env bash
# N06H: run the flaky-bisect simulator (Python stdlib only)
set -euo pipefail
cd "$(dirname "$0")"
python3 -I flaky_bisect_sim.py | tee run_output.txt
