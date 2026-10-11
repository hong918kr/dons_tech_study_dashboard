#!/usr/bin/env bash
# 각 단계를 실제로 돌려서 out/*.txt 에 화면 출력을 저장한다 (L08 페이지가 이걸 그대로 보여 준다).
cd "$(dirname "$0")"
PY=../../.venv/bin/python
run() { local name=$1; shift; { echo "\$ pytest $*"; $PY -m pytest -p no:cacheprovider "$@" 2>&1 | sed -E -e 's/ in [0-9.]+s/ in 0.01s/' -e '/^(platform|rootdir|cachedir)/d'; } > out/$name.txt; }
run step1         step1_first_test.py
run step1_verbose -v step1_first_test.py
run step1b        step1b_failing.py
run step2         -v step2_raises.py
run step3         -v step3_parametrize.py
run step4         -v -s step4_fixture.py
run step5         -v step5_mock.py
run step6         -v step6_ring_buffer.py
{ echo "$ python decorator_demo.py"; $PY decorator_demo.py 2>&1; } > out/decorator.txt
