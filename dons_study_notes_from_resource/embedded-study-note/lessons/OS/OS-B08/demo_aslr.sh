#!/bin/sh
# OS-B08 demo_aslr.sh — demo_layout 을 N 번 실행해 "무엇이 바뀌고 무엇이 같이 움직이나" 표를 만든다.
#   sh demo_aslr.sh [N]          (기본 N=8, 레슨 폴더에서 실행 — demo_layout.c 를 /tmp 에 빌드)
#   Linux(Lima VM)에서도 그대로 돈다:  sh demo_aslr.sh 8   /   setarch "$(uname -m)" -R sh demo_aslr.sh 8
#
# 열 설명
#   distinct  N 번 중 서로 다른 주소 개수 (1 = 고정)
#   d(main)   (주소 - main) 이 N 번 내내 같으면 "same" → main 과 같은 slide 로 함께 움직인다
#             (주소 + main) 이 N 번 내내 같으면 "mirror" → slide 만큼 "반대로" 움직인다 (독립 무작위 아님)
#   pgoff     주소 % PAGESIZE 가 N 번 내내 같으면 "same" (ASLR 은 페이지 단위로만 민다; mac 16K, VM 4K)
#   varbits   N 개 주소의 XOR 차이에서 바뀐 비트 범위 (hi..lo)
set -e
N=${1:-8}
DIR=$(cd "$(dirname "$0")" && pwd)
WORK=/tmp/OS-B08_aslr.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread "$DIR/demo_layout.c" -o "$WORK/demo_layout"
i=0
while [ "$i" -lt "$N" ]; do
    "$WORK/demo_layout" -q | sed 's/^\(.*\) \(0x[0-9a-f]*\)$/\1|\2/' > "$WORK/run$i.txt"
    i=$((i + 1))
done
PG=$(getconf PAGESIZE)
echo "$(uname -sm)  PAGESIZE=$PG  runs=$N"
cat "$WORK"/run*.txt | python3 -I -c '
import sys
from collections import OrderedDict
runs, pg = sys.argv[1], int(sys.argv[2])
rows = OrderedDict()
for line in sys.stdin:
    name, addr = line.rstrip("\n").split("|")
    rows.setdefault(name, []).append(int(addr, 16))
N = int(runs)
mains = rows.get("main (text)")
print("%-24s %8s %8s %9s %10s  %s" % ("object", "distinct", "d(main)", "pgoff", "varbits", "run0 .. run1"))
for name, v in rows.items():
    if name == "main image slide":
        continue
    d = len(set(v))
    dm = "-"
    if mains and len(set(a - m for a, m in zip(v, mains))) == 1:
        dm = "same"
    elif mains and len(set(a + m for a, m in zip(v, mains))) == 1:
        dm = "mirror"
    po = "same" if len(set(a % pg for a in v)) == 1 else "-"
    x = 0
    for a in v:
        x |= a ^ v[0]
    vb = "none" if x == 0 else "%d..%d" % (x.bit_length() - 1, (x & -x).bit_length() - 1)
    print("%-24s %8d %8s %9s %10s  %#x %#x" % (name, d, dm, po, vb, v[0], v[1] if len(v) > 1 else v[0]))
' "$N" "$PG"
