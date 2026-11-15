#!/bin/sh
# ELX-B05: board.dts → board.dtb (dtc) → board_golden.dts (dtc 역컴파일) → board_dtb.h (C 배열 + 골든 문자열)
# 사용: sh make_blob.sh   (Mac, Homebrew dtc 1.8.1)
set -eu
cd "$(dirname "$0")"
dtc -q -I dts -O dtb -o board.dtb board.dts
dtc -q -I dtb -O dts -o board_golden.dts board.dtb
python3 -I - board.dtb board_golden.dts board_dtb.h <<'PY'
import sys
blob = open(sys.argv[1], 'rb').read()
gold = open(sys.argv[2], 'rb').read().decode('ascii')
out = ['/* 자동 생성: make_blob.sh — 손으로 고치지 말 것 (board.dts 를 고치고 다시 실행) */',
       '#ifndef ELX_B05_BOARD_DTB_H', '#define ELX_B05_BOARD_DTB_H', '',
       '/* dtc -I dts -O dtb board.dts 결과 (%d 바이트) */' % len(blob),
       'static const unsigned char board_dtb[%d] = {' % len(blob)]
for i in range(0, len(blob), 12):
    out.append('\t' + ' '.join('0x%02x,' % b for b in blob[i:i + 12]))
out += ['};', '',
        '/* dtc -I dtb -O dts board.dtb 결과 — 파서 출력이 이것과 바이트 단위로 같아야 한다 */',
        'static const char board_dts_golden[] =']
for line in gold.splitlines(True):
    esc = line.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t').replace('\n', '\\n')
    out.append('\t"%s"' % esc)
out[-1] += ';'
out += ['', '#endif', '']
open(sys.argv[3], 'w').write('\n'.join(out))
PY
echo "board.dtb $(wc -c < board.dtb | tr -d ' ') bytes, board_dtb.h written"
