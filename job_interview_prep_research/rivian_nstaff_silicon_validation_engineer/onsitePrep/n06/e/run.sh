#!/bin/sh
# N06E: numbers for E2/E3/E4 pass criteria + CSI-2 ECC/CRC error-injection campaign.
# Python stdlib only; -I = isolated mode.
cd "$(dirname "$0")" || exit 1
echo "===== e_numbers.py ====="
python3 -I e_numbers.py || exit 1
echo
echo "===== csi2_err_inject.py ====="
python3 -I csi2_err_inject.py || exit 1
