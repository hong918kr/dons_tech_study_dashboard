#!/usr/bin/env bash
# ENV: linux (Lima VM elinux 안에서 실행. 어느 cwd 에서든 됨)
# PCIE-B06 check.sh <starter|solution|practice>
#   1. 레슨 디렉터리를 /tmp/PCIE-B06_work/<variant>.$$ 로 복사 (맥 홈은 VM 에서 읽기 전용)
#   2. gcc -std=c11 -Wall -Wextra -Werror -O2 로 빌드 (경고 = 실패)
#   3. sudo 로 실행 (config 256B·capability 를 보려면 root 필요) — 프로그램이 직접 lspci/setpci 를 불러 대조
#   4. PASS/FAIL 줄 + "N/M passed", 마지막 줄 "RESULT: PASS (<variant>)" 또는 "RESULT: FAIL (...)", exit 0/1
# 읽기만 한다: setpci 쓰기 없음, insmod 없음, VM 재시작 없음.
# 사용: limactl shell elinux -- bash <lesson>/check.sh solution
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
V=${1:-}
case "$V" in
starter|solution|practice) ;;
*) echo "usage: check.sh <starter|solution|practice>"; exit 2 ;;
esac
command -v lspci >/dev/null && command -v setpci >/dev/null && command -v gcc >/dev/null || {
	echo "FAIL: need gcc + pciutils (lspci/setpci) — run inside the Lima VM"; echo "0/1 passed"
	echo "RESULT: FAIL ($V, tools)"; exit 1; }
W=/tmp/PCIE-B06_work/$V.$$
cleanup() { rm -rf "$W"; }
trap cleanup EXIT
mkdir -p "$W"
cp "$SRC"/starter.c "$SRC"/solution.c "$SRC"/practice.c "$W/"

if ! gcc -std=c11 -Wall -Wextra -Werror -O2 "$W/$V.c" -o "$W/$V" >"$W/build.log" 2>&1; then
	cat "$W/build.log"
	echo "FAIL: build $V.c"; echo "0/1 passed"; echo "RESULT: FAIL ($V, build)"; exit 1
fi

# 비root 사용자로 들어와도 sudo -n (비밀번호 없는 sudo) 로 root 실행. 실패해도 프로그램은 돈다 (Part C 가 FAIL).
if [ "$(id -u)" = 0 ]; then RUN=""; elif sudo -n true 2>/dev/null; then RUN="sudo -n"; else RUN=""; fi
(cd "$W" && timeout 60 $RUN "./$V") >"$W/run.log" 2>&1
RC=$?
grep -E '^(PASS|FAIL) ' "$W/run.log"
P=$(grep -c '^PASS ' "$W/run.log")
F=$(grep -c '^FAIL ' "$W/run.log")
SUMMARY=$(grep -E '^[0-9]+/[0-9]+ passed$' "$W/run.log" | tail -1)
echo "${SUMMARY:-$P/$((P + F)) passed (no summary line, exit $RC)}"

if [ "$RC" -eq 0 ] && [ "$F" -eq 0 ] && [ "$P" -gt 0 ] && [ -n "$SUMMARY" ]; then
	echo "RESULT: PASS ($V)"
	exit 0
fi
[ "$RC" -eq 124 ] && echo "(timeout)"
echo "RESULT: FAIL ($V, exit $RC, $F failed)"
exit 1
