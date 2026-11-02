#!/usr/bin/env bash
# N06B: regenerate mock results, print exit-criteria report and stats checks.
set -euo pipefail
cd "$(dirname "$0")"
python3 -I -B gen_mock.py
python3 -I -B coverage_report.py --reqs requirements.csv --results results.csv --bugs bugs.csv
echo
python3 -I -B stats_problems.py
