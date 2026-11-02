#!/bin/sh
# Builds/runs everything in onsitePrep/n07/a and writes run_output.txt
cd "$(dirname "$0")" || exit 1
python3 -I capture_odds.py > run_output.txt 2>&1
rc=$?
cat run_output.txt
exit $rc
