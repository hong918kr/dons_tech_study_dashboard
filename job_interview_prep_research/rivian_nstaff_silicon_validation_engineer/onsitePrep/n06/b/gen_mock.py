#!/usr/bin/env python3
"""Generate a deterministic mock silicon-validation results export (results.csv).

One row = one test session on one (chip, board, V/F/T corner, FW build):
the test loops `iterations` times and reports how many iterations failed.
The data is invented for practice; the scenarios are designed so that each
block ends in a different exit-criteria verdict (see N06B section 6).
"""
import csv
import os
import sys

NOM = "NV-Fnom-25C"
PVT = [NOM, "LV-Fmax-125C", "LV-Fmax-m40C", "HV-Fmax-125C", "HV-Fmax-m40C"]
PROC = ["TT", "SS", "FF"]
BOARDS = ["EVB-01", "EVB-02", "EVB-04"]   # EVB-03 is the only camera board
LATEST, OLD = "fw1.4", "fw1.3"

rows = []


def add(block, req, test, chip, board, vft, iters, fails, build=LATEST, result=None):
    if result is None:
        result = "PASS" if fails == 0 else "FAIL"
    rows.append([f"R{len(rows) + 1:04d}", build, block, req, test, chip, board,
                 vft, iters, fails, result])


def pvt_cells(block, req, test, iters, fail_at=None, fails=0, blocked_at=None, board=None):
    """15 cells = 5 V/F/T corners x 3 process corners."""
    for i, vft in enumerate(PVT):
        for j, proc in enumerate(PROC):
            chip = f"A0-{proc}-0{1 + (i % 2)}"
            brd = board or BOARDS[(i + j) % len(BOARDS)]
            if blocked_at == (proc, vft):
                add(block, req, test, chip, brd, vft, 0, 0, result="BLOCKED")
            elif fail_at == (proc, vft):
                add(block, req, test, chip, brd, vft, iters, fails)
            else:
                add(block, req, test, chip, brd, vft, iters, 0)


def nom(block, req, test, iters_list, board=None, fails_list=None, build=LATEST):
    chips = ["A0-TT-01", "A0-SS-01", "A0-FF-01"]
    for k, iters in enumerate(iters_list):
        fails = fails_list[k] if fails_list else 0
        add(block, req, test, chips[k % 3], board or BOARDS[k % len(BOARDS)], NOM,
            iters, fails, build=build)


# ---- DMA: all P0 good, one P1 waived, one P0 test without negative control
nom("DMA", "DMA-001", "TC-DMA-001", [1, 1, 1])
pvt_cells("DMA", "DMA-002", "TC-DMA-002", 20)
nom("DMA", "DMA-003", "TC-DMA-003a", [64, 64])
nom("DMA", "DMA-003", "TC-DMA-003b", [64])
nom("DMA", "DMA-004", "TC-DMA-004", [50, 50, 50], fails_list=[1, 0, 1], build=OLD)  # BUG-DMA-03
pvt_cells("DMA", "DMA-004", "TC-DMA-004", 50)
nom("DMA", "DMA-005", "TC-DMA-005", [10, 10])
pvt_cells("DMA", "DMA-006", "TC-DMA-006", 220)                                   # 3300 runs, 0 fail
nom("DMA", "DMA-007", "TC-DMA-007", [5, 5])
nom("DMA", "DMA-008", "TC-DMA-008", [10])
nom("DMA", "DMA-009", "TC-DMA-009", [10, 10])
nom("DMA", "DMA-010", "TC-DMA-010", [20], fails_list=[1])                        # ERR-DMA-01

# ---- I2C: clean
nom("I2C", "I2C-001", "TC-I2C-001", [1, 1, 1])
nom("I2C", "I2C-002", "TC-I2C-002", [100, 100], build=OLD)
pvt_cells("I2C", "I2C-002", "TC-I2C-002", 100)
nom("I2C", "I2C-003", "TC-I2C-003a", [20, 20])
nom("I2C", "I2C-003", "TC-I2C-003b", [20])
nom("I2C", "I2C-004", "TC-I2C-004", [600, 500, 500])                             # 1600 runs, 0 fail
nom("I2C", "I2C-005", "TC-I2C-005", [10, 10])
nom("I2C", "I2C-006", "TC-I2C-006", [10])

# ---- DDR: worst corner blocked, soak too short, one P1 never run
pvt_cells("DDR", "DDR-001", "TC-DDR-001", 134)                                   # 2010 boots
pvt_cells("DDR", "DDR-002", "TC-DDR-002", 1, blocked_at=("SS", "LV-Fmax-125C"))  # BUG-DDR-11
nom("DDR", "DDR-003", "TC-DDR-003", [20, 20])
nom("DDR", "DDR-004", "TC-DDR-004", [400, 400, 400])                             # 1200 runs, 0 fail
nom("DDR", "DDR-005", "TC-DDR-005", [50, 50])
nom("DDR", "DDR-006", "TC-DDR-006", [10, 10])
# DDR-007: no rows at all (never executed)

# ---- CSI: real failures at one corner, only one camera board
nom("CSI", "CSI-001", "TC-CSI-001", [1, 1, 1], board="EVB-03")
pvt_cells("CSI", "CSI-002", "TC-CSI-002", 10, board="EVB-03")
pvt_cells("CSI", "CSI-003", "TC-CSI-003", 167, fail_at=("SS", "HV-Fmax-m40C"), fails=2,
          board="EVB-03")                                                       # BUG-CSI-17
nom("CSI", "CSI-004", "TC-CSI-004", [10, 10], board="EVB-03")
nom("CSI", "CSI-005", "TC-CSI-005", [5], board="EVB-03")
nom("CSI", "CSI-006", "TC-CSI-006", [5], board="EVB-03")

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "results.csv")
with open(out, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["run_id", "build", "block", "req_id", "test_id", "chip", "board",
                "vft_corner", "iterations", "fails", "result"])
    w.writerows(rows)
print(f"wrote {len(rows)} rows -> {os.path.basename(out)}")
