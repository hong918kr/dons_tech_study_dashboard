#!/bin/sh
# N06A: build the I2C test matrix from i2c_features.json, then show the lint
# going from FAIL (2 deliberate gaps) to PASS after the gaps are fixed.
# Python stdlib only; -I = isolated mode.
cd "$(dirname "$0")" || exit 1
OUT="${TMPDIR:-/tmp}"
echo "===== run 1: plan as written (expect lint FAIL, exit 1) ====="
python3 -I test_matrix_gen.py i2c_features.json
echo "exit=$?"
echo
echo "===== run 2: add the missing ERR rows (DMA abort/bad address; resume with transfer in flight) ====="
python3 -I - "$OUT/n06a_fixed.json" <<'PY'
import json, sys
d = json.load(open("i2c_features.json"))
for f in d["features"]:
    if f["id"] == "F07":
        f["cats"].append("ERR")
        f["axes"]["fault"] = ["bad address", "abort mid-burst"]
    if f["id"] == "F11":
        f["cats"].append("ERR")
json.dump(d, open(sys.argv[1], "w"), indent=1)
PY
python3 -I test_matrix_gen.py "$OUT/n06a_fixed.json" > "$OUT/n06a_run2.txt"
rc=$?
sed -n '/^#### 4/,$p' "$OUT/n06a_run2.txt"
echo "exit=$rc"
