#!/usr/bin/env bash
# L02 예제 전부 실행 → 프레임워크별 PASS/FAIL 요약.
#   bash lecture/frameworks/run_all.sh
# 준비: bash lecture/frameworks/setup_fw_env.sh (Python 3.12 .venv-fw) · bash lecture/frameworks/c/fetch_vendor.sh (C 프레임워크 소스)
set -uo pipefail
cd "$(dirname "$0")"
PY=../../.venv-fw/bin
declare -a R
run() { local name="$1"; shift; if "$@" >/tmp/l02_$$.log 2>&1; then R+=("✓ $name"); else R+=("✗ $name (log: /tmp/l02_$$.log)"); cat /tmp/l02_$$.log | tail -20; fi; }
run "unittest (PyUnit)"       python3 -m unittest unittest_demo.test_dut_unittest
run "pytest"                  $PY/python -m pytest -q pytest_demo
run "Robot Framework"         $PY/robot --outputdir robot_demo/out robot_demo/dut_smoke.robot
run "behave (BDD)"            bash -c "cd behave_demo && ../$PY/behave -f progress"
run "OpenHTF"                 $PY/python openhtf_demo/fc_station.py SN0001
run "py-micro-hil (sim)"      bash -c "cd py_micro_hil && ../$PY/python run_sim.py"
run "Unity + CMock"           make -s -C c/unity test
run "Ceedling"                bash c/ceedling/run.sh
run "CppUTest"                make -s -C c/cpputest test
run "GoogleTest + gMock"      make -s -C c/googletest test
printf '%s\n' "${R[@]}"
