#!/bin/sh
# N06F code: FP tolerance study, NPU roofline calculator, ECC fault-injection campaign.
# Python stdlib only; -I = isolated mode.
set -e
cd "$(dirname "$0")"
for f in fp_tolerance.py roofline.py ecc_fault_inject.py; do
    echo "######## $f"
    python3 -I "$f"
    echo
done
