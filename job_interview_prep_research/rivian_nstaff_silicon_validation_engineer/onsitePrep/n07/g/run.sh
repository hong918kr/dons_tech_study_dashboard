#!/bin/sh
# N07G: regenerate the mock CSI-2 RX event logs, run the stream analyzer on all
# of them, and save the output to run_output.txt. Python stdlib only; -I = isolated.
cd "$(dirname "$0")" || exit 1
{
  echo "===== generate mocks ====="
  python3 -I gen_mocks.py
  echo
  echo "===== analyze ====="
  python3 -I csi2_stream_diag.py mocks/G0_normal.log mocks/G3_ecc_vs_temp.log \
    mocks/G4_diagonal_tear.log mocks/G6_four_cam_drop.log mocks/G7_vc_collision.log \
    mocks/G8_fe_lost_merge.log mocks/G10a_sensor_15fps.log mocks/G10b_buffer_starved.log
  echo "exit=$?"
  echo
  echo "===== test of the test: G4 log relabelled '#expect OK' must FAIL ====="
  T="${TMPDIR:-/tmp}/n07g_mislabel.log"
  sed 's/^#expect .*/#expect OK/' mocks/G4_diagonal_tear.log > "$T"
  python3 -I csi2_stream_diag.py "$T" | grep -E 'diagnosis|expected|SUMMARY'
  python3 -I csi2_stream_diag.py "$T" > /dev/null; echo "analyzer exit=$? (1 = mismatch caught)"
} 2>&1 | tee run_output.txt
