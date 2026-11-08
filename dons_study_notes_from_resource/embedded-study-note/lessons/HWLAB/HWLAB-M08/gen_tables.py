#!/usr/bin/env python3
"""HWLAB-M08 데이터 생성기 (결정적 — 다시 돌려도 JSON 이 바이트 단위로 같다)

  refclk_spec.json  SSDB-4 rev A 의 PCIE_REFCLK 판정 스펙 (적분 구간, PCIe 형 필터 파라미터, 한도)
  osc_Y2.json       후보 Y2: 100 MHz 크리스털 XO (저가형) — close-in 열세, 노이즈 플로어 우수
  osc_Y3.json       후보 Y3: 100 MHz 분수-N PLL 기반 프로그래머블 오실레이터 — close-in 우수, 플로어 열세 + 스퍼

표의 숫자는 "데이터시트 typical 표" 모양을 흉내 낸 합성값이다 (특정 실제 부품 아님).
PCIe 필터·한도 숫자는 대표값 — 실제 판정 전 PCI-SIG Base Spec / CEM 스펙 확인 필요.

  python3 -I gen_tables.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

SPEC = {
    "board": "SSDB-4 rev A (PCIe 4.0 x4 NVMe SSD 컨트롤러 평가 보드)",
    "net": "PCIE_REFCLK",
    "f0_hz": 100e6,
    "band_unfiltered_hz": [12e3, 20e6],
    "band_filtered_hz": [1e3, 50e6],
    "filter": {
        "name": "PCIe common-clock 형 (Gen4 대표값 - 스펙 확인 필요)",
        "pll1_bw_hz": [2e6, 4e6],
        "pll1_peak_db": [0.01, 2.0],
        "pll2_bw_hz": [2e6, 5e6],
        "pll2_peak_db": [0.01, 1.0],
        "delay_s": 12e-9,
        "cdr_hpf_hz": 10e6,
    },
    "link_limit_ps": 0.5,
    "osc_alloc_ps": 0.25,
    "note": "link 한도 0.5 ps(rms, 필터 적용) 중 오실레이터 배정 0.25 ps; 나머지는 fanout buffer, 전원 잡음, 크로스토크 몫",
}

Y2 = {
    "ref": "Y2",
    "desc": "100 MHz HCSL 크리스털 XO (저가형)",
    "f0_hz": 100e6,
    "table": [[100, -82], [1e3, -97], [1e4, -108], [1e5, -121], [1e6, -145], [1e7, -158], [5e7, -160]],
    "spurs": [],
}

Y3 = {
    "ref": "Y3",
    "desc": "100 MHz HCSL 분수-N PLL 기반 프로그래머블 오실레이터",
    "f0_hz": 100e6,
    "table": [[100, -95], [1e3, -118], [1e4, -130], [1e5, -138], [1e6, -140], [1e7, -141], [5e7, -142]],
    "spurs": [[6.25e6, -80]],
}


def write(name, obj):
    with open(os.path.join(HERE, name), "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, ensure_ascii=False, indent=1)
        f.write("\n")


if __name__ == "__main__":
    write("refclk_spec.json", SPEC)
    write("osc_Y2.json", Y2)
    write("osc_Y3.json", Y3)
    print("wrote refclk_spec.json osc_Y2.json osc_Y3.json")
